# Client coverage: baseline tools

Work toward the client coverage plan (readher's `CLIENT_COVERAGE_PLAN.md`, 2026-11-10). It tracks
three measures separately: original-function accounting, port function execution, and port line and
branch execution. This file covers step 1 (a reproducible baseline) and the start of step 2 (the
address ledger). Nothing here measures execution yet; that is step 3.

## Harness suites: `_re/harness/run_suites.py`

One runner and manifest for every harness build script under `_re/harness` (37 suites).

    python _re/harness/run_suites.py                 all suites (about 25 minutes)
    python _re/harness/run_suites.py "net.*" crowd   by name or pattern
    python _re/harness/run_suites.py --list          the manifest

- It writes `report.md` and `report.json` to `%TEMP%\hd2_suites` (`--work`). The report records the
  revision, uncommitted changes, MinGW and MSVC, the data folder, and per suite the status, the
  parsed counts, the time, the executable's hash and the log.
- Each suite's executable is deleted before its build, so a failed compile is reported as
  `build-failed` and never runs a stale binary. Many build scripts carry on to copy and run after
  a failed g++.
- Output is bounded (the last 256 KB per run) and runs are killed at their time limit.
- The vehicle builds run their inputs in 12 parallel chunks, each in its own working folder,
  because tests write scratch files to the current folder.
- Inputs are made from the game data (`--data`, default `H:\HD2_Unpacked`):
  - the 37 car models and 176 damage models (`export_cars.py`);
  - the 22 missions (each one with a `Vertanim.bin`, plus AFRICA2);
  - the cars' property blobs (`carparams.py`).
- Prerequisites: MinGW at `C:\nim-2.2.6\dist\mingw32`, MSVC `vcvars32.bat` (crash.msvc), and the
  game data. `checkpoints` and `profile` have `H:\HD2_Unpacked` hard-coded in their build scripts.
  `mp_setup` needs its map lists and profiles (`--mp-data`), which aren't on this machine.
- Not run: vehicle_params' original-reader comparison (ORIG_DIR), and its `apply.exe` (built only).
  The mutation lists are not part of the baseline run.

### Baseline, 2026-11-10

The first run (at 59f8b0b0) gave 33 pass, 1 fail, 2 crashed, 1 missing prerequisite. All three failures were out
of date tests, not port bugs:

- **vehicle.text and vehicle.factory crashed.** The bar test passed the pool list a raw buffer with
  a map name at +1. Commit f807aab5 makes the pool list read a real `C_MpMap` (its name is at +4 in
  the port), so the raw buffer crashed it. The test now passes a `C_MpMap`.
- **vehicle.text, hotkey.hkParts.** Commit 76b55336 posts `MENU_MSG_KEYBOARD_OFF` with value 1, as
  the original does (0x0064eec0), and OnKeyClick forwards to OnClick; the test's two checks still
  expected 0.
- **net.session.** Commit 38a422a7 hosts with `DPNSESSION_NODPNSVR` as well (flags 0x41); the test
  still expected 1.
- **vehicle.factory, description.dsWrite.** This was the runner's fault: parallel chunks in one
  folder shared the test's `mpdesc_test.txt`.

A full run at c96abfef, which also has PR 13 merged, gave 34 pass, 2 build-failed and 1 missing
prerequisite (mp_setup). Both crash builds failed to link: fa665363 added the crash report dialog,
which reads `g_texts`. crash_test.cpp now fakes the text and the builds link `c_str.cpp`, and both
pass. That leaves 36 of 37 passing, with mp_setup not run.

So the runner catches failing checks, crashes, link failures and broken inputs.

The profile suite rewrites two tracked files in `profile/run` (`profile1.sav` and
`PlayersProfiles/Tester/profile.sav`). Restore them with `git checkout -- _re/harness/profile/run`
after a run.

## Address ledger: `tools/address_ledger.py`

    python tools/address_ledger.py                   writes _re/coverage/ledger.csv, prints a summary
    python tools/address_ledger.py --list open:live  the rows of one disposition

One row per original function in the game range: 12,306 functions and 3,861,288 bytes, with unwind
and catch funclets left out as `port_stats.py` does.

- **Disposition:**
  - `hand`, `absorbed`, `idiom` or `empty`;
  - otherwise `open:<triage bucket>` (live, tiny, static, dead, dead-eh?).
- **`src`:** the exact source lines naming the address. There is no nearest-address credit.
- **`section`:** the `ported.txt` section the address is listed under.
- **`review`:** taken from `_re/ledger_review.txt` (`ADDRESS DISPOSITION text`). A review never
  changes the measured disposition; the summary counts reviewed rows separately.

| Disposition | Functions | Original bytes |
|---|---:|---:|
| hand | 9,609 | 3,425,433 |
| absorbed | 1,168 | 183,578 |
| idiom | 287 | 6,103 |
| empty | 8 | 40 |
| open:live | 534 | 60,203 |
| open:tiny | 304 | 2,946 |
| open:static | 6 | 163 |
| open:dead | 278 | 175,650 |
| open:dead-eh? | 112 | 7,172 |

Findings for step 2:

- 1,680 `hand` rows (169,604 bytes) and 1,048 `absorbed` rows (80,097 bytes) have no exact address
  in the sources.
- `ported.txt` doesn't only list ported code. Some of its non-absorbed sections list code marked
  done so that tier 1 stops generating it, and their headers say so: "unreferenced in this exe ...
  not ported", "code without callers (not ported)", static-guard thunks, STL shapes. The largest
  such group is 641 rows under "versions" (64 KB), which needs a look.
  - A rough match on such headers covers 1,057 `hand` rows (441 KB). That is an upper bound to
    review, not a count of unported code.
  - Either way, `hand` overstates "ported by hand". Reviewed rows should get their own disposition
    (unreachable, std replacement, absorbed by a named function).

## Stats tools

`tools/port_stats.py` and `tools/coverage_map.py` read this repository's own `_re` and `src`.
`coverage_map.py --exe-only` needs no DLL index. The LS3DF index isn't in the repository: pass
`--ls3df-re` or set the env `LS3DF_RE`. Without it, the LS3DF rows are left out with a note.
`coverage_map.py`'s "verified" state is still based on source-name regexes. The plan replaces it
with the ledger plus measured results (step 6).

## Step 3 trial: measuring execution (2026-11-10)

No clang, llvm-cov or OpenCppCoverage is installed. The tools at hand are MinGW's gcov (the
harnesses) and MSVC (the client). The trial used both.

### Harnesses: gcov (lines, branches, functions)

A production source compiled with `--coverage`, and the harness linked with it. The run writes
`.gcda`; `gcov --json-format -b` gives lines, functions and branch outcomes per source. Tried on
the formation suite (`src/game/formation.cpp`):

- 605 of 607 lines; 37 of 37 functions; 481 of 531 branch outcomes taken.
- The two lines never run are both early returns: an empty saved chunk (formation.cpp:151) and a
  squad member without a controller (:393).
- The harness's own files are measured too but kept apart: only the production object counts.
- This is the MinGW/SSE build, not the shipping client's ABI or x87 code, as the plan notes.

Since then wired into the runner: `python _re/harness/run_suites.py --coverage`. Each build script
runs from a temporary copy whose g++ line gets `--coverage -dumpdir <notes>`. Each run, and each
vehicle chunk on its own, writes its `.gcda` to its own folder through `GCOV_PREFIX`. gcov reads
every folder, and the counts of sources under `src/` (not tier1, not the tests' files) are added up
per suite and over all suites: `coverage/coverage.md` and `coverage.json.gz` in the work folder.

**First full measurement** (revision 15d91609, 35 MinGW suites; mp_setup has no data, crash.msvc is
MSVC). The report is `_re/coverage/harness_coverage.md`.

| Measured files | Lines | Functions | Branch outcomes taken |
|---|---:|---:|---:|
| 273 (154 `.cpp`, the rest headers) | 53,050 / 55,743 (95.2 %) | 4,698 / 5,174 (90.8 %) | 37,873 / 45,754 (82.8 %) |

The percentages cover only what the harnesses compile:

- 117 of the client's 271 `.cpp` files (104,813 of about 197,000 source lines) are in no suite at
  all. The largest are the humans (locomotion, equipment, movement, animation), shots, the AI,
  squad actions and the interface's commands: the plan's P1 packages. They are listed at the end
  of the report.
- Inside the measured files, the vehicle, menu and net sources are almost fully run (controls.cpp
  3,456 of 3,476 lines, main_menu.cpp 2,023 of 2,030). That fits tests that were strengthened
  until mutations failed.

### Client: function entry with MSVC `/Gh`

- `python _re/tools/build_game.py --coverage` builds into `%TEMP%\hd2_game_cov`.
  - Every source gets `/Gh`: each function's first instruction becomes `call _penter`. The rest of
    the code is the release build's.
  - The hook is `_re/coverage/penter.cpp`, compiled without `/Gh`.
  - It marks the function in `HD2_Port.cov` beside the exe (or `%HD2_COVERAGE_FILE%`). The file
    is memory-mapped, one byte per image byte, and stamped with the build.
- `python _re/tools/client_coverage.py FILE.cov [...]` maps hits to the map file's functions and
  checks each function's first bytes for the call. Several runs of one build are ORed together.
- The port's switch `-nowait` keeps the game running without the focus. Without it, LS3DF pauses
  an inactive window (" ..zzZZ"), so a scripted run stalls when something else takes the focus.

Checks:

- **Instrumentation.** All 11,736 of the port's function symbols carry the call. The rest:
  - 4,318 exception funclets: never instrumented, reported apart.
  - 1 compiler-made adjustor thunk.
- **Mapping.** Every hit is exactly a function start + 5 (0 stray hits).
- **Execution changes the report.** One unattended run (`-window -nowait`), read three times:
  - during the videos: 208 port functions entered;
  - at the main menu: 502, with the menu code (controls, main_menu, text_box) now entered;
  - after a clean exit: 526.
  Earlier runs that lost the focus stayed at the videos' count.
- **Killed process.** A small MSVC test with the same hook ended by `TerminateProcess` kept its
  hits; its never-called function stayed unmarked.
- **x87 behaviour.** That test's x87 sum printed the same digits with and without `/Gh`.
  `_penter` saves EAX/ECX/EDX and the flags and never touches the FPU.

Limits:

- **Function level only.** Lines and branches of the MSVC client need another tool: clang-cl's
  source coverage (its codegen isn't MSVC's), or a breakpoint-based line tracer on the PDB such
  as OpenCppCoverage (lines, no branches). Neither is installed.
- **Inlining.** Functions inlined everywhere have no symbol; their bodies count as the callers'.
- **Units.** The 11,736 port symbols aren't the ledger's original functions. Matching them up:
  see below.
- **Not run.** The game wasn't run in a mission, in multiplayer, or killed by a real crash. The
  kill test is the small program.
- **Timing.** The `/Gh` build is slower (a call per function entry), so it isn't a timing reference.

### Port functions to original addresses: `_re/tools/port_symbols.py`

For each function symbol of the port's own objects, the PDB (dbghelp, offline) gives the file and
line of its first instruction. The original address is then looked for in three places:

- **definition:** the head line, walking back to the line naming the function, and the comment
  lines right above it;
- **declaration:** a header line naming it inside its class's body, read through to its `;`;
- otherwise none.

Results:

- 7,456 port functions (definition 3,262, declaration 4,194) name an address. Over all symbols,
  8,008 distinct game-range addresses are named.
- Without an address: 4,600 port functions. Most are the port's own helpers (lambdas,
  anonymous-namespace functions) or functions the original only had inline.
- A comment that mentions another function's address (a caller, a variant) can link the wrong
  one. The `found_by` field says where the address came from.

client_coverage.py uses it to count original functions as well. In the menu run:

- 7,278 of the ledger's 9,609 `hand` rows are named by a port function, and 450 were reached.
- 57 of the 1,168 `absorbed` rows are named, and none were reached (absorbed helpers are mostly
  inline in their callers).
- The 2,331 unnamed `hand` rows overlap the ledger's rows without an address comment, and its
  sections of code marked done without a port.

## New harness: shots (part 1, 2026-11-10)

`src/game/shots.cpp` was in no suite; it is the largest uncovered file that doesn't need a
`C_Human`. `_re/harness/shots`: `build.bat` builds `shots_test.cpp` with `shots_globals.cpp` and
the real `shots.cpp`. The engine and the game are faked:

- the collision query hands back scripted hits;
- the scene's `IntersectFrame` gives scripted answers;
- actors are zeroed bytes with a vtable that logs `Hit` and `GetMaterialId`;
- sounds, scripts' frame hits, impulses and version-keeper damage are logged.

The suite runs as `shots` in run_suites.py: 155 checks.

Part 1 is the bullet: `C_BulletShot::Init`, `GetDamageAt`, `CheckNearMiss` and `Tick`. The
expectations come from the original's assembly (0x0044b3b0, 0x0044b910, 0x00448380).

- **Tested:** free flight and gravity; the flare's half time; the 600 m end, and the extra tick a
  client keeps; PASS / STOP / no material; sorting; hit sounds and effects by role; penetration
  costs; power over distance; the ricochet (`RandFloat()*0.1 + 1.1`, velocity × 0.2, damage ×
  0.3, head-on at −0.95); level meshes drawn in the last frame; doors, physics objects and
  destructible parts; cars and the shooter's own vehicle; static weapons (hit with the step,
  before the visual test); the one-hit-per-actor rule; fake shots; near misses within 3 m.
- **Coverage:** `Tick` runs 125 of its 138 lines. shots.cpp as a whole: 257 of 1,482 lines.
- **Mutations:** `mutations/mutate_shots1.py` (146 mutants, from `gen_mutations.py`). 56 killed,
  10 don't build, 80 survived. The survivors are equivalent ones, listed with reasons in the
  file, and code part 2 will test.
- **Part 2a (done):** humans and hit zones, the trace and flare visuals (`Update`), and bullet
  decals. Added:
  - logging fakes for the frame vtable (SetPos, SetDir, SetWorldDir, Update, SetOn, LinkTo,
    FindChildFrame, Release and the projector's slots), the driver's `CreateVisual`, and the
    scene's light and sector;
  - the real `c_str.cpp`.

  Expectations from 0x00455670 (`GetHitZone`), 0x0044b400 (`Update`) and 0x0044ebd0
  (`AddBulletDecal`):
  - zones 0..9 in order, each put on its joint at scale 1, a type-0 helmet tried before the head;
  - the squad not hitting itself in a local game; `NotifyShotAt` on a miss;
  - the trace hidden for its first 30 m, its distance kept as an int: 30 ms of an 80 m step is 23
    m, not 24, since `30 * 0.01f` is just under 0.3;
  - the flare's glow and sparks between 30 and 100 m, its burn-out and the signal-pistol flag;
  - decals: the type filters, the skinned-parent walk, the 0.4 / 1 light steps, reuse of the
    oldest, and the hit part or no frame through `Tick`.

  312 checks. `mutations/mutate_shots2a.py`: 149 mutants, 128 killed, 9 don't build, 12 survived
  (11 equivalent, 1 without an exact float input; listed there). shots.cpp: 393 of 1,482 lines.
- **Part 2b (done):** the projectiles, the client bullets, and the manager's core. Expectations
  from 0x00449290 (the projectile's `Tick`) and 0x00451fb0 (the manager's `Update`):
  - the projectile:
    - its explosion share: 0.5 after any static surface, 1 for a human, 0.1 for a car;
    - a car hit directly 0.1 m before the hit point, ×4 for the player's side on difficulty 1
      with a model;
    - penetration while its damage is at least the cost, hit effects for materials 31..34, 41,
      43, 62 and 70;
    - the explosion 0.2 m off the surface; the camera riding along after 300 ms, lingering 1.5 s;
  - the client bullet: the 500 m ray with its filters;
  - the manager:
    - a pure client's 100 ms ticks (> 100), with arrived bullets recycled to the pool;
    - the save placeholders, the explosion message, the firing roles, `RemoveShots`.

  690 checks. `mutations/mutate_shots2b.py`: 273 mutants, 172 killed, 16 don't build, 85
  survived. 12 survivors are equivalent and 4 can't be killed with this harness; the reasons are
  in the file. The rest is effects code for part 2c.

  shots.cpp: 733 of 1,482 lines (49 %), 558 of 1,075 branch outcomes.
- **Part 2c (done):** the effects. Tested:
  - thrown items: `OnContact` with the squad rule (<= 200 ms), penetration and destructible
    parts, plus fuses, impacts, the 15 s loss and the first-bounce sound;
  - shells: the velocity formula, bounces, getting stuck, three bounces, the first-person copy,
    reuse from the pool;
  - muzzle lights: the portals within 4 m, the fade, the flash at 50 ms, out at 100 ms;
  - blood: pools growing every 100 ms, drops landing or giving up after 1 s, splats;
  - the mission's start, stop and restart, and the blood save placeholders;
  - `MeleeAttack`: backstab, humans, dogs, static weapons, other actors;
  - `SendExplosionMessage`, the fly sound, the explosion decal;
  - `IsSegmentInView` (coarse cases only), `FireBullet`'s dummies, `ThrowItem`.

  Fakes: a vtable per kind of engine object, because the light, sound, sector and projector
  reuse the same slot offsets with different arguments, which breaks stdcall.

  690 → 967 checks. Mutations:
  - `mutate_shots2c.py`: 524, 455 killed, 41 don't build, 28 survived (all equivalent,
    unreachable or not observable);
  - `mutate_shots2d.py`: 57, 25 killed, 7 don't build, 25 survived, all in `IsSegmentInView`,
    whose clipping and projection arithmetic still needs a reference written from its assembly
    (0x00450620).

  shots.cpp in all: **1,419 of 1,482 lines (96 %), 96 of 101 functions, 941 of 1,075 branch
  outcomes (88 %)**. Not run:
  - the trace and flare models' creation (`CreateTrace`, which needs a fake mesh chain);
  - `C_HitZones::Place` / `SetOn` (no callers in the port);
  - the manager's destructor;
  - the unreachable paths named in `mutate_shots2c.py`.

`_re/harness/mutrun.py` now takes its repository root from its own path. It used to name the
G: checkout, which has been out of date since the move to T:.

## New harness: equipment (part 1, 2026-11-10)

`src/game/human_equipment.cpp` (C_HumanEquipment: the models a soldier carries) was in no
suite. `_re/harness/equipment`: `build.bat` builds `equipment_test.cpp` with
`equipment_globals.cpp` and the real `human_equipment.cpp`, `equipment_frames.cpp`, `chunk.cpp`
and `c_str.cpp`. The LS3DF port's `math.cpp` is linked too, built for x87 with PC_24 set as the
game runs it, because a loaded quaternion goes through `SetRot`, which normalises it.

- **Fakes:** the engine frames get one vtable that logs its calls. It includes the model's
  animation stages, the visual's light level, the driver's `CreateFrame` and the scene's
  `FindFrame`. `C_Human`, `C_HumanMovement` and the weapon pose are zeroed bytes
  (`#define private public`), with their containers, items and definitions as small subclasses.
  Chunks are read from and written to memory.
- **Part 1 tests:**
  - the holsters: setup in the holster pose (checked against the asm), `GetHolsterSlot`'s
    behaviour and type tables (0x00791b80), the counts shown per frame, the light level, hiding,
    release and setting up again;
  - `SetModel` / `GetModel` for every place;
  - save / load.
- **Save / load against the original:** the round trip reads a quick save made with the
  original game. It isn't in the repository: by default it's
  `%TEMP%\eq_saves\original_SMCAM_quick.sav`, or set env `EQUIPMENT_SAVE`. Without it the suite
  reports missing-prerequisite. Its 49 equipment chunks (0x309) are each loaded and saved again:
  1,644 of 1,663 fields come back the same. The other 19 are rotations within 4 ulps: the saved
  quaternions aren't exactly unit length, and the original's `SetRot` renormalises them too.
  Then each field is varied on those chunks: the actors given to / taken from (0 and -1
  included), the special object found in the scene by its saved name, models of uid 0 or -1 or
  saved as none, the long gun and backpack switched off, the long gun hidden by the movement's
  state, and the defaults after a second load of an empty chunk.
- **Results:** 400 checks. `mutations/mutate_equipment1.py`: 318 mutants, 290 killed, 19 don't
  build, 9 survived. Those 9 are equivalent, or wait for part 2; the docstring lists them.
- **Coverage:** human_equipment.cpp **524 of 1,274 lines, 23 of 42 functions, 412 of 1,156
  branch outcomes**.
- **Still to test:**
  - part 2: `AttachModel` / `DetachModel`, `Update` (the long gun's tilt, slide and lift),
    animation events;
  - part 3: the item callbacks, `ShowHandsItem` / `PutAway`, the flashlight;
  - the hands and special models' other load paths (belt, thrown, props, actors).

### Equipment part 2a: AttachModel / DetachModel (2026-11-10)

- **Fakes:** the frames' world matrices are now computed: the local matrix through the
  parent's. `SetWorldDir` turns the local matrix (the joints sit at the origin), and
  `UpdateBound`, `CloneFrameTree`, the scene's sector calls and the collision sweep are faked.
  The local player and the first-person view can be set.
- **Tests:**
  - every place attached and detached: the head (stains by weight), the backpack and the
    aqualung's diving gear, the long gun, the hands item in the hand and put away on the back
    (place 10), and the special object in both hands, in one hand, and on the animation
    (places 4..9);
  - put down, put back, released, thrown, with and without something in the way.
- **Geometry:** the long gun's and the put-away rifle's local matrices are checked against a
  reference that follows the original's steps, with the constants read from its asm
  (0x0078e89f.., 0x007900ff..). That check found a port bug: place 10 used -1.428f where the
  original has 0xbfb6c898 (-1.4279966). Fixed on main (fd1afd66).
- **Your save as a geometry reference:** I tried it and it doesn't work. The long guns saved
  there sit where the H:\HD2_Unpacked models' backlink dummies can't put them (w_98k's frame 1
  is offset, w_98ks's isn't, yet their saved positions differ only in x). Sabre Squadron
  probably ships its own weapon models. The original's `BacklinkOffset` does what the port does
  (0x0078e992). The head and backpack offsets do match the saved positions.
- **Results:** 594 checks. `mutations/mutate_equipment2a.py`: 282 mutants, 257 killed, 16 don't
  build, 9 survived, all equivalent (listed there).

### Equipment part 2b: events, Update, names, helpers (2026-11-10)

- **Tests:**
  - `OnAnimationEvent`: the support hand, give and take between two humans, the launcher's
    round (items get a vtable; the fake slots come from g++'s own member pointers because g++'s
    vtable isn't MSVC's), and the special places' events 4..15;
  - `Update`: the hands time running out, and the long gun's lift against a step-by-step
    reference following the original's order (0x007932ac..0x0079355e); the flashlight's and
    lens flare's times;
  - `UpdateModelNames`: the temporary names by behaviour, capacity and headgear kind, and when
    the model is shown;
  - `Unknown793b40`, `Unknown7938f0`, `GetDropPosition`, `Unknown792310`, `FindJoints`.
- **An original drift:** the lift slides the gun out and then tilts it (turning the slid
  position as well), and on the way back undoes them in the other order. So each full lift
  leaves the long gun about 1.1 mm lower. The port does the same; the test pins it.
- **Results:** 731 checks. `mutations/mutate_equipment2b.py`: 172 mutants, 143 killed, 20 don't
  build, 9 survived. Eight are equivalent; the lens flare's waits for part 3.
- **Coverage after part 2:** human_equipment.cpp **934 of 1,274 lines, 31 of 42 functions,
  768 of 1,156 branch outcomes (66 %)**.
- **Still to test (part 3):** `ShowHandsItem`, `PutAwayHandsItem`, the item callbacks
  (`OnItemCreated/Added/Removed`), `SetFlashlight`, `UpdateLensFlare`, and LoadGame's
  special-object paths other than the scene's.

### Equipment part 3: the hands item, the item callbacks, the flashlight, the lifetime (2026-11-10)

- **Tests:**
  - `ShowHandsItem` (hide, show, toggle) and `PutAwayHandsItem` (small things hidden, rifles on
    the back, back into the hand, the first-person view told for a client's active player);
  - `OnItemCreated/Added/Removed` for every container, with hand-overs to and from living,
    dead and put-away humans;
  - `SetFlashlight` and `UpdateLensFlare` (lights and sectors get their own fake vtables,
    because their slots reuse the model's offsets);
  - LoadGame's hands links and every source of the special object (belt kinds 14 / 19,
    type 0xE5, thrown items, activity props, actors, the scene);
  - `Init`, `Done`, `Reset`, the destructor, and save / load on a closed chunk.
- **Checked against the original:** `PutAwayHandsItem` sets the put-away flag before it reads
  the support hand (0x0079211b, 0x0079212c). So putting a rifle away never lets the support
  hand go at that point. The port matches.
- **Results:** 971 checks. Mutations: `mutate_equipment3.py` 196 mutants, 177 killed, 16 don't
  build, 3 survived (equivalent); `mutate_equipment3c.py` 21 mutants, 19 killed, 2 don't
  build, none survived.
- **human_equipment.cpp in all: 1,273 of 1,274 lines, 42 of 42 functions, 1,055 of 1,156
  branch outcomes (91 %).** The one line not run is the cap of 4 frames a holster slot: no slot
  of the real table has more than 3 dummies.
- **Mutations over the file:** 989 mutants in parts 1, 2a, 2b, 3 and 3c: 889 killed, 73 don't
  build, 27 survived, all equivalent (listed per file). Part 1's lift reset and part 2b's lens
  flare were caught once part 3's tests were in.

`mutrun.py`: it now takes the g++ line with the most `%SRC%` sources, because the equipment
bat compiles math.cpp first. Env `MUT_EXTRA_SRCS` / `MUT_EXTRA_INC` add sources from outside
`src/`, which are compiled once and not mutated. The equipment run:
`MUT_HARNESS=equipment MUT_EXTRA_SRCS=ls3df/src/math/math.cpp MUT_EXTRA_INC=ls3df/src`.

## New harness: items (part 1, 2026-11-10)

The item system (`items_module.cpp`, `item_instances.cpp`, `item_defs.cpp`,
`item_containers.cpp`, about 3,500 lines) was in no suite. `_re/harness/items` links all four
as they are, with chunk.cpp, c_str.cpp and the LS3DF math. Everything else is faked and logged:
the engine (no models load), the humans' and the equipment's callbacks, sound, shots,
particles and the surface marks. Files are served from memory.

- **Inputs:**
  - `ITEMS_SAV`: by default `G:\Hidden & Dangerous 2\Tables\items.sav`, the loose table the
    original reads ahead of the packed one. It is 252,000 bytes, the port's `ITEMS_TABLE_SIZE`,
    and differs from H:\HD2_Unpacked's 2003 copy.
  - `EQUIPMENT_SAVE`: your quick save from the original, as for the equipment suite.
  - The suite reports missing-prerequisite without either.
- **Definitions:** all 272 of the table are written back by `C_ItemDef::Save` as the table holds
  them. Each is written twice (over 0x00 and 0xEE) so unwritten bytes show. The sizes come from
  an independent table of component stream sizes, and the derived fields are checked where the
  layout puts them.
  - Not compared: 11,182 padding bytes. The editor that wrote the table left them 0xCD / 0xCC
    (debug fills). The original copies a component stream into the object and back whole
    (0x007df370 / 0x007df330), so it would keep them; the port goes field by field and writes 0
    or nothing.
  - Nothing in the game saves definitions, so this can't affect play.
- **Your save:** all 69 actor item chunks (0x2775) load into a human's containers (7 into an
  item store) and save back byte for byte. The exceptions are 909 container (0x66) and owner
  (0x67) fields:
  - The original's `C_Item::LoadGame` reads those under 0xCA / 0xCB (its byte table at
    0x007d4514); only `MoveItem` sets them again.
  - So a loaded item saves container 0 and no owner until it moves: an original weakness the
    port keeps.
  - The module chunk (0xB256, the last uid 551) comes back the same.
- **Port bug found:** the owner read under 0xCB used `GetActorById` where the original calls
  `GetActor` (by index). Fixed on main (2ac8bb2c); no save of the game holds 0xCB.
- **Also tested:**
  - the containers' rules (slot, list, store: sorts, the first free slot, the slot shift,
    full);
  - the loaded actors' carried weight and backpack contents;
  - each of the 272 types' new-item values;
  - items.sav missing, unreadable, or opened once from the disk;
  - destructors, medkits, and the 0xCA / 0xCB ids.
- **Results:** 2,410 checks. `mutations/mutate_items1.py` (four sources, merged: labels M / D
  / I / C + line): 441 mutants, 399 killed, 20 don't build, 22 survived. They are equivalent,
  or out of bounds in the original too; the docstring lists them.
- **Coverage:**

  | File | Lines | Branch outcomes |
  |---|---:|---:|
  | item_containers.cpp | 109 of 109 | 98 % |
  | item_defs.cpp | 372 of 459 | |
  | item_instances.cpp | 180 of 454 | |
  | items_module.cpp | 194 of 964 | |

- **Still to test:**
  - part 2: moving items (`MoveItem`, `PutItem`, `TransferItem`, `CanPutItem*`, the result
    codes, weight and space limits, swaps, drops);
  - part 3: the weapons (reload, fire, sounds, projectiles) and the mission items file.

### Items part 2: moving items (2026-11-10)

- **Tests** (the harness now gives actors a vtable for `GetHitPoints`, raw equipments for the
  logged callbacks, an interface, a first-person view and a scene):
  - `MoveItem`: every container's checks one at a time, the order where two fail, boundaries,
    locks, the same place, test-only; then the bookkeeping: weights, the owner and container
    index, busy times, replaced items, backpacks with their contents, items lying in the world;
  - `NotifyMove`'s callbacks, locks and first-person events;
  - `PutItem` / `CanPutItem` / `PutItemAuto`, `CreateItem` / `GiveItem` / `CreateFreeItem`;
  - `TransferItem`: every swap shape with its exact equipment and first-person callbacks
    (including the displaced item put elsewhere or dropped, and uniforms on a server), and
    `TransferItemAuto`;
  - `IsSwapBlocked`'s checks one by one, `CanAddItemToInventory` / `CanPutItemInSlot`,
    removing and dropping;
  - `GetAllowedContainers` / `GetInventoryGroup` over every behaviour and size.
- **Checked against the original's asm:** `MoveItem`'s hands and backpack cases, and the start of
  `TransferItem`. The port matches.
- **Found:** `TransferItem` passes `IsSwapBlocked` a null source when the source isn't a human,
  and `IsSwapBlocked` reads it when the target place is taken. The original does the same
  (0x007da274..), so a vehicle's item into a taken slot crashes there too. It isn't tested and I
  haven't changed it.
- A local game has the server bit, so items moved in the last 920 ms are busy. The tests wait a
  second before each transfer.
- **Results:** 4,098 checks. `mutations/mutate_items2.py`: 424 mutants, 363 killed, 25 don't
  build, 36 survived. All 36 are equivalent; the docstring lists them.
- **Coverage:** items_module.cpp 704 of 964 lines (74 % of branch outcomes); item_defs.cpp 405
  of 459.
- **Still to test (part 3):** the weapons (`C_WeaponItem`: reload, fire, sounds, projectiles),
  the mission items file, the module's mission lifecycle, and the activity props.

### Items part 3: weapons, the mission items file, the lifecycle (2026-11-10)

- **The mission items files:** all 75 `items.dat` of H:\HD2_Unpacked\MISSIONS (env
  `MISSIONS_DIR`) go through the real `LoadMissionItems`. The scene fake makes an actor for every
  name a record asks for: an item store when all its records are in container 0 (boxes, fixed
  guns), a human otherwise.
  - The 74 complete files load as an independent parse expects: 325 placed items with their
    exact position, rotation, armed and empty flags, and 20,997 items in their actors.
    `SaveMissionItems` writes every actor's records back the same.
  - normandy3_mp_zone's file ends in the middle of a record; the load returns false, as the
    original's reader does on a short read.
  - The original also reads `nameLength + 1` name bytes (0x007d87ae), so the placed records with
    no name line up. My first parse got that wrong, not the port.
  - Also tested: the file's failures (cut short at each field, names of 127 / 128 characters,
    versions 13 / 14 / 15, short armed / empty reads, write failures).
- **The module's lifecycle:** mission data, start / stop / restart per pass, freeing, the
  surface marks' ticks (server, and clients in 100 ms steps), uids, `Done`; the activity props;
  copying and releasing frame trees.
- **The weapons:**
  - `Init`, `Use`, `GetAmount`, the dummies;
  - `Reload`: every refusal; the fullest and emptiest magazine; the rest; infinite ammunition;
    the last magazine kept; pending reloads; the launcher's contents; the sound and ready times.
    Checked against the original's asm (0x007d4ee3..0x007d5030); the port matches.
  - `SetRounds`, the empty click, `StopReloadSound` (including a wrapped clock);
  - `Fire` by kind: thrown, firearm with tracers every third round of SMGs and MGs, projectile
    with its blast, melee by role; `FireSecondary`;
  - the projectile found, copied and attached; save / load;
  - the shot sound frames, the AI's noise (silenced types 7 / 22 / 260 carry 0.3 as far), the
    action sounds of every weapon type in the table.
- **Note:** a local game has both the server and the client bit, so the client-side sounds play
  too.
- **Results:** 25,678 checks. `mutations/mutate_items3.py`: 454 mutants, 398 killed, 33 don't
  build, 23 survived. They are equivalent, leaks, unreachable, or out of range only in the
  fakes; the docstring lists them.
- **The item system in all:**

  | File | Lines | Branch outcomes |
  |---|---:|---:|
  | items_module.cpp | 913 of 964 | 87 % |
  | item_instances.cpp | 442 of 454 | 87 % |
  | item_defs.cpp | 405 of 459 | |
  | item_containers.cpp | 109 of 109 | |

  Mutations over parts 1–3: 1,319 mutants, 1,160 killed, 78 don't build, 81 survived
  (equivalent or untestable, listed per file).
- **Not tested:** the definitions' model, image and diving-gear loaders (`GetModel`,
  `GetFpvModel`, `GetImage`, `GetDivingGear`), which need the engine's models.

### Items part 4: the human's items, `human_items.cpp` (2026-11-10)

- **The build:** a second build of the items harness, suite `items.human`
  (`items/build_human.bat`). It is items_test.cpp with `HUMAN_ITEMS_TEST` and
  `human_items_test.inc` / `human_items_test2.inc`, linking the real `game/human_items.cpp` on
  the real item system and `items.sav`. Faked and logged: the controller (its virtuals by g++
  slot), the temporary actors (their `DropItem` takes the item out as the real one does), supply
  boxes, item actors, zones, the sight module, the effects' `DressUniform`.
- **Tested:**
  - `Init` / `ClearContainers` / `Done` and the slot getters;
  - every lookup and count, each container and its search order (slots from their ends; the
    belt's last ready slot first);
  - `RemoveItem` from each container (the hands: reload sound, view mode) and
    `RemoveItemsFromStore`;
  - the requests to the controller, including the uniform-swap rules, and `DrawLongGun` /
    `DrawSidearm` for every reason;
  - the server and client moves and transfers;
  - `DropItem`, `ClientDropItem` and `DropAllItems`, with the random velocities replayed from a
    seed;
  - the supply boxes: charges, states, the item lists by team, no room;
  - picking up; `PutOnUniform` and its client version;
  - the soldier state: uids only outside a local game, and an empty item 38 left out in single
    player;
  - `DropCarried`, the action's progress (seat weapons of the four vehicle kinds, zones).
- **Checked against the original's asm, the port matching:** `RequestThrowItem` (0x005a2c50);
  `DropAllItems`' base direction, velocities and hands rule (0x005a2fe0); `CountAmmo`
  (0x005a6c50); `GetEquipmentState` (0x005a8b50); `ServerMoveItem`'s lookup (0x005a2d50).
- **An original weakness, kept:** `ClearContainers` (0x005a29c0) leaves the backpack contents'
  pointer to the store it just freed with the backpack. Respawn and `CreateEquipment` clear the
  pointer first (`ClearActorContainers`); the plain mission stop does not.
- **Also as in the original:** a store actor's items can't be taken with `ServerMoveItem`. Its
  container 0 is read as the head slot, and a store has no item there; the client side finds them
  anywhere.
- **Results:** 418 checks. `human_items.cpp`: lines 499 of 500, functions 72 of 72, branch
  outcomes 490 of 526 (93 %); the rest are inline null checks and unreachable branches.
  `mutations/mutate_items4.py`: 260 mutants, 215 killed, 29 don't build, 16 survived
  (equivalent, leaks, a debug message, one unreachable; listed).

## New harness: actors (the actor lists, `actors_module.cpp`, 2026-11-10)

- **The build:** suite `actors` (`actors/build.bat`). It links the real `game/actors_module.cpp`
  with `net/bitstream.cpp` (the real message encoding, with a player table quantising 2 km in
  1 cm steps), `common/chunk.cpp` and the LS3DF math. The actors are raw objects made by fake
  creators in `g_actorCreators`, with a logging vtable (g++ slots). Faked and logged: the items
  module (its own simple item records), the humans, frames and their data, collision, the net
  game, the interface.
- **`C_ActorsModule`:**
  - creation: types out of range or without a creator; player slots and their use count; an
    occupant replaced; a failing creator; frame data made or rebound;
  - destruction: the last actor taking the freed place; frame data freed only when nothing else
    uses it; delayed removal of humans and their lookup by id and as a human;
  - the active set and the changes queued while it is iterated (`Tick` / `Update` order);
  - free player slots; spawn points, the random one replayed from a seed;
  - removals by role, including several in one tick;
  - start, stop and restart by role; the enemy counts (every condition, carnage enemies, co-op);
  - save / load byte layout and activity; state and messages (the 10-bit index, player slots
    to the human's handler); the message helpers; `Clear`; the destructors.
- **`C_TemporaryActorsList`:**
  - creation (ids, start by role) and destruction (deferred while iterating, stop by role, the
    interface told on clients); `DestroyMinesOf`;
  - mines: laid (the owner's item taken, the multiplayer message's fields), on clients, loaded;
  - dropped items and the client's version (new or taken from the owner; the exact facing);
  - range and nearby queries; debris; breakable fences (on clients only without the server bit);
  - the mission start: armed mines laid by a ray 0.5 m above, 2 m down, or at their place; items
    at their place and rotation; multiplayer pick-ups kept; stop / restart by role;
  - save / load chunks and activity; `WriteState` → `ReadState` round trip from server to client
    through the real encoding; the 16-bit next id; the messages and commands.
- **Checked against the original's asm:**
  - `TickRemovals` (0x00476860): the entry moved into a freed place has its time taken off
    once. The port did the same, but its comment said twice; the comment is corrected.
  - The role tests of `Tick` / `Update` (0x00476080 / 0x00476160): a pure server removes in
    both, a client in `Update` only, a local game never.
  - `LoadMine` (0x00478e50) and its call (0x004781f5) take the id, then the item. The header's
    parameter names were swapped; corrected.
- **Original weaknesses, kept:**
  - a saved mine whose model doesn't load makes `LoadGame` call through null;
  - `DestroyMinesOf` skips a mine of the owner moved into a freed place;
- **Restored on main (2026-11-10):** `C_TemporaryActorsList::LoadGame` keeps the previous
  chunk's actor, as the original (0x00477f50) does. The original resets only the active flag (to
  1) per chunk, so a debris or unknown chunk that follows an inactive item, mine or fence switches
  that actor on. The port had started each chunk with no actor; that version is on the branch
  `temp-actors-load-reset`.
- **Results:** 672 checks. `actors_module.cpp`: lines 656 of 656, functions 80 of 82 (the two
  destructors' other compiled copies), branch outcomes 467 of 472 (99 %).
  `mutations/mutate_actors.py`: 373 mutants, 312 killed, 41 don't build, 20 survived
  (equivalent, out-of-range undefined behaviour, debug text; listed).
- **A harness note:** a crash at exit (static destructors after the fake creators) left
  processes that `taskkill` couldn't stop, holding the executable. They had to be ended through
  WMI (`Win32_Process.Terminate`). The test now turns off crash dialogs (`SetErrorMode`).

## New harness: collision part 1 (loading, rays, volumes, 2026-11-11)

- **The build:** suite `collision` (`collision/build.bat`, input: the levels' `tree.klz`). It links
  the four collision sources and the LS3DF math.
- **Levels:**
  - all 82 levels' files are read and checked against my own parse of the format (header, tables,
    the cells' entries in z, y, x order);
  - NORMANDY3_MP_ZONE's file is a 16-byte stub, like the rest of that mission, and is skipped;
  - without the scene, every game triangle is disabled (they all use the scene's visuals), so on
    the real levels the primitives are what get tested.
- **Rays on the real levels:** 161,963 random segments (500 per level in the quick mode). The grid
  walk (`RayTest`), `RayTestBrute`, `RayTestStatic` (lowest layer), `QuerySegment` and my own ray
  tests of boxes, axis-aligned boxes, spheres, rectangles and poles agree.
- **Collision files written by the test (`WriteSynth`):**
  - triangles of all seven kinds, one- and two-sided, water, plus capsule and convex primitives;
  - the kinds the files store, checked against the loader's classification of the lowest layer
    (including its thresholds and ties);
  - fake visuals for the game's own path: a mirror, a model where a visual is required, a frame
    not found, degenerate triangles at their thresholds.
- **Dynamic volumes:** boxes, world boxes, spheres, capsules and convex hulls. Covered: the grid
  (adding, moving, releasing, clamping, ignored volumes, `ClearGrid`), every ray query, and exact
  normals and places.
- **Region queries:** `GetVolumeAt`, `GetVolumesInRadius`, and `TestBoxSweep` checked against my
  own reading of its rules (my levels, BURMA1, Co_Sicily1).
- **Other levels:** a level whose corner is not at 0, a non-square level, and a single-cell level.
- **Infinite and huge segments:** answered without hanging (the knife-kill fix, 43d5ac75).
- **Original weaknesses, kept (checked in the original's asm, the port matching):**
  - **Boxes:** a segment starting inside a box parallel to a face hits it at once (0x006f46e0).
  - **Capsules:** a segment starting inside a capsule's infinite cylinder, beyond an end, misses
    it (0x006f4d70).
  - **Vertical segments:** `QuerySegment` finds no static geometry along them (0x006ff510; its
    jump table 0x00700f60 visits no cell).
  - **Box sweeps:** `TestBoxSweep` uses the squared half diagonal as a radius (0x006f0ac0).
  - **Clearing the grid:** `ClearGrid` deletes an emptied but unreleased cell and leaves it in the
    grid (0x006eff70); callers must `ReleaseEmptyCells` first.
- **Results:** 1,163,769 checks.
  - `collision.cpp`: lines 503 of 511, functions 35 of 36.
  - `collision_ray.cpp`: lines 526 of 526, functions 51 of 51.
  - `mutations/mutate_collision1.py`: 869 mutants, 603 killed, 93 don't build, 173 survived: 102
    boundary comparisons, the rest leaks, unused values, margins and early stops (listed).
- **A harness lesson:** the first two mutation runs were void. The test wrote its files to fixed
  names in %TEMP% and the runner's parallel copies overwrote each other's; files are now per
  process.
- **Not in part 1:** sphere sweeps and sliding (`collision_sweep.cpp`, `collision_body.cpp`):
  part 2.

## New harness: collision part 2 (sphere sweeps, slides, body sweeps, 2026-11-11)

- **The build:** the same suite (`collision`; `collision_test.exe sweeps` runs part 2 alone),
  tests in `collision/sweep_test.inc`, and a new suite `collision.sweepdiff` (`sweep_diff.cpp`).
- **The original executable as the oracle (`collision/original_exe.h`):**
  - the unpacked `HD2_SabreSquadron_unpacked.exe` is put at its own base, 0x400000, inside the
    test process. The test is linked at 0x20000000, relaunches itself suspended with that range
    held, copies the sections by hand and binds the imports to the game's DLLs;
  - its `SphereTest`, `SlideSphere`, `MoveSpheres`, `MoveSpheresStepping` and `MoveSpheresLow` are
    called on the port's own world (the same layout). The dynamic grid is passed as a copy with
    MSVC6's 16-byte vectors;
  - results must match within the x87's 24-bit precision against the port's floats.
- **Also my own exact sweep** (a ternary search and bisection on the distance to each shape's
  core). Where the original's sweeps approximate, mine differs and the original decides.
- **Scenes:**
  - the levels' primitives (23,280 sweeps);
  - a level of my own: triangles of every kind, a tilted rectangle, capsule and convex primitives;
  - dynamic volumes of every sphere-sweep shape: boxes, world boxes, spheres, cylinders, the
    second sphere kind, scaled boxes, capsules and convex hulls;
  - ground of my own for the bodies: a floor, steps, walls, ramps, a V of steep slopes, a narrow
    slot, a roof, an overhang, humans standing and lying, crates.
- **Results against the original:**
  - `SphereTest`: every query agrees (54,594).
  - Body moves: `MoveSpheres` 3,500 of 3,501, `MoveSpheresStepping` 3,561 of 3,561,
    `MoveSpheresLow` 3,498 of 3,498. The one other is accounted for as below.
  - `SlideSphere`: about 99% agree; every difference has a shown cause.
- **How chain differences are judged:** slides and body moves are chains, and each step's rounding
  goes into the next. The steps are written out again in the test and must give the port's chain
  over the port's own sweeps. A difference is accounted for only if one of these holds:
  - over the original's sweeps, the written-out steps give the original's chain or the port's;
  - the port moved 0.1 mm gives the original's;
  - the chain was stuck touching one place when the two parted.
- **`sweep_diff`, single shapes against the original:**
  - boxes, edges, triangles and capsules (200,000 sweeps each);
  - every static primitive kind through `SweepSpherePrimitive` (300,000 sweeps);
  - all agree.
- **Port bug found and fixed on main:** `MoveSpheresLow` starts again from just above the ground
  after meeting a human. The original then caps the contacts at `g_moveIterations - 1`
  (0x0071cf4e); the port used the full count.
- **Original weaknesses, kept (the port matches each):**
  - convex hulls take their entry however far ahead and keep it as a squared fraction of the move,
    so a shape tested after one can be met beyond the move's end;
  - a capsule volume is swept with the direction left in world space, so a laid-down capsule (a
    lying human) is mostly missed;
  - a capsule gives no contact when the move goes away from its axis, though it would meet an end
    cap;
  - box, rectangle and triangle sweeps test only the edges the leading point passes outside of,
    take the first edge met, then one corner, and can miss an earlier contact;
  - above 6 iterations, `MoveSpheresLow` can write an 11th contact past its 10-entry table. The
    locomotion mostly uses 2–6; one branch adds 1.
- **A data convention:** a triangle whose corners, sorted by x, wind along its stored normal needs
  `COLLISION_TRIANGLE_REVERSED` for sphere sweeps. The game's 2.3 million triangles have none; the
  test's own files (`Tri`) now set it.
- **Results:** 1,275,855 collision checks (part 1 and 2).
  - `collision_sweep.cpp`: lines 567 of 567, functions 23 of 23, branch outcomes 605 of 635.
  - `collision_body.cpp`: lines 224 of 224, functions 8 of 8, branch outcomes 151 of 156.
  - `mutations/mutate_collision2.py`: 736 mutants, 534 killed, 71 don't build, 131 survived: 76
    boundary comparisons, the rest equivalent, within tolerance or not reached (listed). 2 more are
    killed by `sweep_diff`.
- **Harness lessons (my mistakes):**
  - the game's DLLs reset the FPU to 53 bits, and until restored the original disagreed;
  - the original writes 16-byte vectors, which overran 12-byte locals;
  - the test's own triangles lacked the reversed flag, so half were inside-out;
  - a first, looser chain excuse (my copy of the steps matching the original) proved the copy, not
    the port: it hid the `MoveSpheresLow` bug's cases and, later, mutants B 81/82.
- **Not exercised:**
  - convex sweeps alone at function level (only through `SphereTest`);
  - the step going up and meeting something above (B 231);
  - the low sweep's restart after a human when contacts were already recorded (B 276/279);
  - mutrun doesn't run `sweep_diff`.

## New harness: physics (objects, the module, boxes, doors, fences, trees, 2026-11-11)

- **The build:** suite `physics` (`physics/build.bat`, `physics_test.exe`; `physics_test.exe motion` runs part 2
  alone). Tests in `module_test.inc` (part 1), `motion_test.inc` (part 2), `break_test.inc` (part 3),
  `ctor_test.inc`. Fake LS3D frames (the engine's layout; world matrices by LS3DF.dll's own `UpdateWMatrixProc`), a
  fake scene, driver and model cache, ground of my own (`collision/synth_level.inc`, shared with the collision suite).
- **Part 1, the module (own reading):** handles and lists through creation, destruction, waking and resting (3,000
  random operations, the invariants after each), the callbacks and their kinds, which role ticks which type, the
  scene's `phobj` objects from their properties, the doors' state message both ways, frame sync and volumes.
- **Part 2, against the original executable** (`collision/original_exe.h`; objects copied into the original's layout
  by a field table, its own vtable, its world a copy of the port's):
  - every type's step, `ApplyImpulse`, `Hit`, `Bounce` and `SyncFrame`: about 160,000 calls, all agree;
  - every constructor (1,400): the original's run twice, on 0xCD and on zeros, to tell the fields it never writes;
  - in its loaded copy, the original's AddVolume / MoveVolume / RemoveVolume and AddBreakable go to loggers and
    GetGame / I3DGetDriver / GetGEffects to the fakes, so a step that adds or removes a volume is compared (which
    volumes end up in the world), and the objects keep their volumes (the grid copy names the original's copy of
    them, which the original ignores by address);
  - five other random seeds agree as well.
- **Part 3, fences and trees (own reading of 0x00407bd0, 0x00406740, KnockOverTree, 0x004079f0):** the original's
  model cache cannot run here. Breaking with and without a scene, parts left out of the model, the collision boxes
  (dummies and scaled visuals), every reference count, the destructor giving the old fence back; trees swapped for
  the damaged model, with and without the part, the model missing, no owner, no scene; a world without cells.
- **Port bugs found and fixed on main:**
  - the physics callback: the original calls it `__fastcall` (kind in ECX, handle in EDX, context on the stack) with
    the kinds created 0, bounced 1, pushed 2, deleted 3, resting 5 (no game code sets one);
  - the sphere's volume: the model's bound times the largest world scale, centred on the bound's center in world
    space (0x0040f7ff..), not the ball's radius at the frame; its step copies the local bound center and radius
    (0x0040f650);
  - boxes: the roll axis's sign (0x0040d21a); the axis ends and the doors' corners with `operator*=` as the original,
    not `operator*` (another summation order: a door's axis differed by a few ulps); the distances that pick a box's
    end to test summed z first as the original's inline code, since the two ends are equally far from the contact and
    the rounding decides (one roll step went the other way).
- **Original weaknesses, kept:** a door pushed while a tree reads its rattling flag unset (heap garbage in the
  original; the port's is false); the swing's and door's never-written fields (listed by the constructor test).
- **Results:** 151,557 checks.
  - `physics.cpp`: lines 613 of 625; `physics_box.cpp`: 163 of 169; `physics_door.cpp`: 304 of 311.
  - `mutations/mutate_physics1.py`: 848 mutants, 647 killed, 68 don't build, 116 survived: boundary comparisons and
    the rest equivalent, within tolerance or not reached (listed).
- **Harness lessons (my mistakes):**
  - a 7-byte jump written over a 6-byte import thunk broke the next thunk (Rotate): thunks are patched by their slot;
  - frames given a scale by writing `m_scale` lost it on the first relink: `SetScale` marks the matrix;
  - the first comparisons skipped every step that moved a volume, and removed doors' and swings' volumes first, which
    hid the swings' settling and the doors' volume handling until the original's world calls were logged.
- **Not exercised:** the effects module's fade-out of debris, the null-frame guards, rays excluding switches for balls
  (they run without the dynamic grid), a debris piece bouncing up onto ground of another height.

## New harness: human part 1 (the locomotion on the ground, 2026-11-11)

- **The build:** suite `human` (`human/build.bat`, `human_test.exe`; needs the original executable). Tests in
  `human/locomotion_test.inc`; the original's side in `human/original_side.inc`.
- **Every call against the original executable's:**
  - the port's human, movement and work are built (the movement's and the work's in-class initialisers through
    constructors defined by the test; the human as raw memory with a vtable of loggers);
  - before each call they are copied into the original's layout. The locomotion, the animation mixer, the work and
    the movement's other parts already have it. The actor, the human and the movement (after its 12-byte
    `std::vector`) do not: their members are copied by tables generated from the headers' offset comments
    (`_re/tools/layout_offsets.py`, new: it also lists every member at another offset). Pointers to the port's
    objects are rewritten to the copies;
  - the original's function runs on the copy, against a copy of the port's collision world
    (`collision/original_world.inc`, now shared with physics);
  - the result is compared field by field with a copy taken of the port's state after its own call. Floats are
    compared within 2e-4; pointers, flags and integers exactly;
  - what the locomotion calls outside itself is faked on both sides alike: the port's functions are defined by the
    test, the original's redirected to the same loggers (`RedirectOriginal`, now in `original_exe`). Each call is
    logged with its arguments and the two logs must agree. What they answer is scripted, consistent with the
    human's fields (the original also has IsServerSimulated inline);
  - the original's own small helpers (AngleBetween2D, RotateAboutX/Y, GetCollisionMaterial,
    S_HumanAnimRef::Normalize, IsSingleStageAnim) serve both sides; they belong to parts 2 and 3.
- **The world:** a floor, ramps of 15 to 65 degrees with open high ends, steps, a wall, platforms of 3, 6 and 12 m,
  a beam, a low roof, a pond, a low wall only the player's squad meets (flag 0x800000), a truck (a dynamic box).
  Actors to walk into: a human, doors (closed, opening), a ladder, a barrel, a train, a physics object, a box of no
  actor, a volume only the local player meets (type 0x100). Each side finds its own object behind a volume's frame
  data (the original's layout: type +0x18, position +0x30, a door's motion +0x224).
- **The calls:** 1,500 trajectories of 40 steps from random states, every one compared. The states vary:
  - the game's role (local, server, client) and the human's (player, enemy, neutral, ally); the local player or not;
  - stance, gait and animation; the movement flags (cheap ground ray, path, remote, small, hidden);
  - fall speeds and heights, pushes, edges, shifts, water; places on the truck, by the actors, over the pond.
  Also 3,000 UpdateFall calls with random arguments.
- **Results:**
  - MoveOnGround: about 59,900 calls, UpdateFall 3,000, all agree (62,896 checks);
  - lines of part 1's code (the contact helpers, MoveOnGround, SweepBody, SlowOnSlope, AdjustToGround, UpdateFall,
    ApplyFallDamage, Land): 633 of 659;
  - `human/mutations/mutate_human1.py`: 723 mutants, 503 killed, 37 don't build, 183 survived: code of parts 2
    and 3, results the caller does not read, values overwritten, rare branches and boundary comparisons (listed).
  - No port bug was found in this part.
- **Undefined words not compared** (the original writes garbage there, the port a defined value, as
  human_locomotion.cpp says): the w of the human's position, direction and velocity, of the locomotion's vectors,
  and of the vectors passed to what is faked.
- **Harness lessons (my mistakes):**
  - a placeholder pointer of 1 in the remapping table turned every word equal to 1 into an address (the movement's
    +0x1AC);
  - this image is not at its preferred base (ASLR): an address range test must use the module's real range;
  - scripted answers that contradict the human's fields gave false differences where the original inlines a check;
  - the fake game had no scene (a crash on a moving dynamic floor);
  - backslashes collapsed twice more in Python run through bash heredocs: scripts now go through files.
- **Not exercised:** a ladder's OnCollisionEx agreeing (the original's deque would allocate), lying bodies' second
  rays, the cheap ground mode's step up on a client, the replicas' ground ray.

## Human part 2 (the locomotion's update, turning, the velocity, 2026-11-11)

- **The tests:** `human/locomotion2_test.inc`, in the same harness and the same way as part 1 (each call against the
  original executable's, on copies in its layout).
  - **TestTurnsAndVelocity:** 18,000 calls, one function each in turn: UpdatePitch, UpdateVelocity, IsTurnAllowed,
    GetTurnTime, SetTurnTarget, UpdateTurn, ChangeStance, UpdateDetail and ScaleStanceVolumes. They run from random
    humans: facing (pitched, not of unit length), the animations now and coming, the work's requests (the asked move,
    stance, carry mode), the turn target, the counters, a seat, a ladder in use, the camera and the frames' render
    stamps (UpdateDetail's level of detail).
  - **TestUpdates:** 3,000 UpdateCollisionVolumes calls, half of them as a client sees another soldier (the frame
    drifted from the body by a little or a lot, the volumes' timer at its edges). Also 3,000 trajectories of 12
    Update steps on the ground.
- **New in the harness:**
  - the weapon pose block, the equipment, the volumes and the scene's camera (+0x15C in the client build) are
    snapshotted and compared;
  - the frame's SetPos and LinkTo calls are counted per side, and its place is logged at UpdateDummies;
  - C_HumanWork::SetLookAt's fake keeps the asked move, as the real one does (UpdateVelocity reads it back);
  - the player team's active player can be the test human (each side its own copy);
  - SetStanceChange is run, not logged: the original has it inline in ChangeStance;
  - the original's Add/Move/RemoveVolume are no-ops (the port's are removed after its call);
  - words of shared objects written inline (`g_sharedWords`) are compared per side.
- **Results:**
  - Update 36,000 calls, UpdateCollisionVolumes 3,000, the others 2,000 each, all agree (119,896 checks with part
    1);
  - lines of part 2's code (RelaxRoll, MoveHitVolumes, the speed table, Update, UpdateCollisionVolumes, and lines
    2204-2878): 716 of 742. Not run:
    - Update's dispatch to swimming, climbing, jumping, the ladder and dying (part 3);
    - UpdatePitch on a ladder with no ladder;
    - UpdateVelocity on a ladder with a held track (the original passes an uninitialised vector there);
    - a look angle below -3 rad;
    - the local player's drift test (unreachable: the local player takes the first branch);
    - UpdateTurn's steps past 2.25 rad (unreachable within the turn limit);
  - `human/mutations/mutate_human2.py`: 632 mutants, 496 killed, 37 don't build, 99 survived. Of those, 21 are in
    part 3's dispatch; the rest are boundary comparisons of floats, counters clamped to 0 either way, unreachable
    lines, or equivalent mutants (listed in its docstring).
  - No port bug was found in this part.
- **Undefined behaviour of the original, kept out of the comparison:** UpdateVelocity on a ladder animation without
  a ladder (an uninitialised direction) or with a held track (an uninitialised vector). The port defines both,
  commented in human_locomotion.cpp. The tests give ladder animations a ladder and no hold.
- **Harness lessons (my mistakes):**
  - CanTurn takes the angle by pointer in the original;
  - the camera is at scene +0x15C in this build (I put it at +0x13C first, which crashed);
  - a fake that only logs (SetLookAt) hid code that depends on its effect (L 2568 survived until it kept the value);
  - the frame's SetPos / LinkTo were not observed at first (L 637, 693 survived);
  - the human frame's random rotation never took effect: `m_rot.Make` with `|= FRMFLAGS_ROT_DIRTY` is ignored once
    the local matrix is valid. Found in part 3 and fixed (SetRot). Part 2's 72 remaining survivors were rerun with
    the frame turned, and none was killed.

## Human part 3 (climbing, ladders, swimming, dying, saved games, 2026-11-11)

- **The tests:** `human/locomotion3_test.inc`, the same way as parts 1 and 2. The original's functions run on
  copies in its layout, and every call is compared.
  - MoveClimbing and MoveJumping (4,000 calls). The animation's head dummy sits under a turned root of its own; the
    climb's heights and times vary.
  - MoveOnLadder and GetLadderPos (4,000). Up, down, sliding, idle and getting on, at the ladder's foot, middle and
    top. Also anywhere in the world, and into the floor and the low roof, so the sphere tests touch.
  - MoveSwimming, PitchTowards, StartSwimming and StopSwimming (6,000).
  - MoveDying, MoveDeadBody, AlignDeadBody and GetSlideOff (6,000). The death states vary, and so does the blood
    pool, bodies on ramps and on the floor, lying and upright joints.
  - The swimming sound (Start, Stop, Update), Init, ResetState, Reset and Done (6,000). The parachute test of the
    player's squad runs high up.
  - Update through all its moves (3,000).
  - SaveGame and LoadGame (2,000 each). Both sides write through the same files (the original's rw_data imports
    are redirected to the harness's file fakes; its C_chunk code runs on the port's C_chunk objects, the same
    layout), and the files must be byte for byte the same. Saves are loaded back into another state, and so are
    made-up files: chunks in any order, unknown ones, another chunk id, nothing open.
- **New in the harness:**
  - joint frames (F1, D1, C1), the head dummy and its root, the shadow;
  - a fake I3D_sound (SetVolume, GetLength through its vtable);
  - the camera-under-water global written for the original (0x98a6ac, read inline);
  - fakes that keep the original's unused locals harmless: S_SoundRequest::Init, and LoadGame's C_fstr;
  - `HUMAN_PART3_ONLY` builds part 3's tests alone (for its mutation runs).
- **Port bugs found and fixed (on main; both are port mistakes, not flaws of the original):**
  - the climb's start and end times (C_HumanMovement +0x960 / +0x964) are floats in the original: OnAnimStarted
    stores ms / 1.4 with FSTP, MoveClimbing reads them as floats. The port had ints (truncated times), and its
    saved games wrote int bits where the original's have float bits;
  - MoveDeadBody, nothing hit: the original adds the move to the position as UpdateFall left it (UpdateFall
    slides the body by 0.9 of the move first: 1.9 of it in all). The port added it to the position read at the
    start. The double move is the original's own: kept, commented;
  - also: StartSwimSound asks whether the sound is heard before checking for one playing, as the original does
    (only the calls' order changed).
- **Results:**
  - every function of part 3 agrees with the original (159,396 checks with parts 1 and 2, 0 failures);
  - lines of part 3's code: 898 of 904 (the whole file: 2,255 of 2,284). Not run:
    - getting on a ladder near its top with LADDER_UP held (the original's UpdateVelocity reads an uninitialised
      vector there; the tests keep ladder animations unheld);
    - MoveDying's slide for states 2, 3 and 6 (they return at the top: unreachable);
    - AlignDeadBody's limit of the drop by the middle sphere (rare geometry);
  - `human/mutations/mutate_human3.py`: 750 mutants, 607 killed, 45 don't build, 98 survived. They are boundary
    comparisons, unreachable lines, rare geometry, or the same result (listed in its docstring).
- **Undefined words not compared:** the w of the blood pool's place and normal (m_unknown9F0 / A00), copied in
  undefined by the original.
- **Harness lessons (my mistakes):**
  - the head dummy's parent never turned (the same rotation mistake as above). The mutant deleting
    `step *= parent` survived until then;
  - a test that only compares two written files passes when both are empty: the size is checked too.

## Human part 4a (the movement base: dummies and volumes, 2026-11-11)

- **The build:** suite `human.base` (`human/build_base.bat`, `human_base_test.exe`). It is the human harness built
  with `HUMAN_BASE`: game/human_movement_base.cpp is linked and its fakes are left out, the original's movement base
  functions run instead of being redirected, and only `human/base_test.inc`'s tests run. The calls take the
  original's movement (the locomotion's copy - 0xAC0).
- **The world's volume calls:** the port's AddVolume, RemoveVolume, MoveVolume and SetIgnoredVolume(s) are wrapped
  with the linker's `--wrap`. On the human's own volumes (stance, hit, trail) they are logged and not done; others
  go to the real world. The original's are redirected to the same loggers, so both sides log the same calls (with
  the volumes' place, size, flags and type) and the two logs must agree.
- **The tests:** 15,000 calls from random humans: UpdateHitSpheres, MoveHitSpheres, UpdateStanceVolumes,
  GetStanceSpheres, UpdateDummies, UpdateLieDummies, MoveDummies, OnFrozen, SnapToGround, ClearFlags,
  Remove/RestoreCollision, SetMorph, SetSeatMode, SetPushState, IsLeftFootForward, SetPosition, RelinkJoints and
  RelinkModel. The model has shot and stance dummies and feet; it is turned and placed on or off the body. The
  states vary: the counts, the stance change, the push state, frozen or not, the volumes' scales, dead or alive.
  The volumes and the model's dummies are kept apart per side and compared whole.
- **New in the harness:**
  - `g_sharedFrames` (frames each side starts from: the model's dummies move with the model);
  - the original's joint-name table filled by its own static initialiser (0x7830e0: the test's image never ran its
    initialisers);
  - shared words compared within the float tolerance.
- **The original's undefined behaviour, kept out:** UpdateStanceVolumes gathers the spheres straight into its four,
  overrunning them with two stances at once, and indexing with the -10000 of a missing dummy. Its count stays
  unclamped. The port fixes both, and the human_movement_base.cpp comment now says why neither happens in the game:
  m_nextStance2 is only ever none, and every human model in the game data has all nine stance dummies (checked
  against H:\HD2_Unpacked\MODELS). The tests keep one stance and all the dummies for UpdateStanceVolumes. Dummies are
  missing only for the functions that skip them.
- **Results:** every function agrees (16,000 checks, 0 failures). Lines of part 4a's code: 321 of 326 (the five not
  run are the excluded cases above, and an AddVolume of hit volumes the function never reaches).
  `human/mutations/mutate_human4.py`: 230 mutants, 210 killed, 8 don't build, 12 survived (equivalent or a
  boundary; listed). No port bug found.
- **Undefined words not compared:** the w of the dummies' places and offsets.
- **Harness lessons (my mistakes):**
  - the frames under the model were not restored between the sides: SnapToGround moved them on the port's side,
    and the original started from there;
  - the shared words were restored before the port's state was taken;
  - the shot dummies were one above the other, so a broken direction (a zero vector normalises to straight up)
    went unseen.

## Human part 4b (the movement base: command record, footsteps, sounds, damage, 2026-11-11)

- **The tests:** `human/base2_test.inc`, in the HUMAN_BASE build. 16,000 calls:
  - S_MoveRequest Reset, SaveGame and LoadGame: random records; the saved files compared, and loaded back; a chunk
    of another id; nothing open;
  - SetOwnerBase;
  - DamageByMaterial and DamageByContacts: the damaging and exploding materials, repeats among the contacts, no
    contacts;
  - StartUseSound and StopUseSound: a ladder with its climb sound or none, a sound already playing or not;
  - PlaySound for others than the active player;
  - Footstep: the ground's material or a ray down, the stealth roll, the sound by gait and stance, inside or
    outdoors, the AI hearing it or not, first person, the sand dust of remote humans;
  - the carrier's side of OnPickUpBody and OnDropBody.
- **New in the harness:**
  - the original's sound module, a fake object answering through its vtable (Play, Stop, SetChannelLimit);
  - every sound request logged whole on both sides;
  - the original's S_SoundRequest::Init filling the defaults;
  - one material table both sides read;
  - fakes for particles, the first-person view, the equipment's models and events, and the network's explosion
    message.
- **Port bug found and fixed (on main; a port mistake):**
  - StartUseSound (a ladder's climbing sound) set no flags and played at the joint itself. The original asks
    SOUND_SILENT_TO_AI | SOUND_KEEP_SLOT and puts the sound 0.4 m below the pelvis joint (0x0075f240).
  - Without KEEP_SLOT the slot could be given to another sound once this one ended, and StopUseSound would then
    stop that one. The AI also heard the ladder.
- **Results:** every function agrees (35,000 checks with part 4a, 0 failures).
  `human/mutations/mutate_human4b.py`: 229 mutants, 207 killed, 12 don't build, 10 survived (defaults already set,
  equivalent, boundaries; listed).
- **Undefined words not compared:** S_MoveRequest's unknown10.w and facingDir.w after Reset (the original copies
  undefined ones in; the port's comment said it left them, now corrected). Also the saved record's three pointers
  (each side's own objects; LoadGame clears them).
- **Not exercised (part 4c):**
  - the footprint (surface marks: the original's footprint kinds are an inline table, its marks a pool of
    visuals);
  - the body's side of picking up and dropping (a second human with its model, vehicle and transparency);
  - UpdateBreath;
  - PlaySound for the active player, UpdateSounds, ProcessRemoteActions, ResetBase, InitBase, DoneBase,
    RestartBase, SaveBase and LoadBase. They grow or free the movement's sound vector and remote action deque: the
    original's MSVC6 containers (its vector is 16 bytes, so the original's fields after +0x194 sit 4 bytes later)
    allocate through its statically linked C runtime's heap, which the test never starts. (Part 4c below takes most
    of them.)

## Human part 4c (the movement base: resets and the sound vector, 2026-11-11)

- **The tests:** `human/base3_test.inc`, in the HUMAN_BASE build. 6,000 calls: PlaySound for the active player (the
  sound kept in the vector), UpdateSounds (finished sounds returned, the last one moved into their place; all of
  them when stopping), ResetBase, RestartBase, ProcessRemoteActions with no action.
- **New in the harness:**
  - the original's operator new and delete (statically linked) are redirected to the test's malloc and free, and
    its rand (its C runtime's per-thread data) to the test's;
  - the port's sound vector is copied into the original's MSVC6 vector before its call (allocator, first +0x198,
    last +0x19C, end +0x1A0; a snapshot taken before the port's call) and compared after it;
  - the fake sound answers IsPlaying;
  - `_re/tools/layout_offsets.py` reads an anonymous union's members that have their own offset comments
    (C_HumanMovement::m_seat, +0xA40, was not copied or compared before).
- **Results:** every function agrees (47,000 checks with parts 4a and 4b, 0 failures).
  `human/mutations/mutate_human4c.py`: 139 mutants, 137 killed, 1 doesn't build, 1 survived (an unused
  computation). No port bug found.
- **Not exercised:**
  - InitBase and DoneBase (they allocate and free the body volumes, which the test owns);
  - SaveBase and LoadBase;
  - ProcessRemoteActions with actions, which needs the MSVC6 deque rebuilt;
  - UpdateBreath (the backpack's aqualung and the copy protection test);
  - the footprints and the body's side of carrying (part 4b's list).
- **Harness lessons (my mistakes):** the raw copies of the movement restore a std::vector's header, not what it
  points to (the port's call had already changed it: the original started from a shuffled vector until the snapshot
  was taken before the port's call); the relink tests cleared joints later tests read.

## Human part 5 (the ragdoll, 2026-11-11)

- **The tests:** `human/ragdoll_test.inc`, in the HUMAN_BASE build (build_base.bat now links
  game/human_pose_physics.cpp, _ragdoll.cpp and _ik.cpp; `-DRAGDOLL_ONLY` runs these tests alone). A 23-bone
  skeleton under the test human's model, the ragdoll built by the port and kept as the rest state:
  - **one function at a time** (9,000 calls from random states: placed anywhere, by the contact actors, over the
    pond; points shaken and moving, flags, normals, materials; pushes and holds, some full; on a vehicle or not;
    the body's detail): IntegrateRagdollPoints, UpdateRagdollBodies, ConstrainRagdollBody, ApplyRagdollCorrections,
    CollideRagdoll, CollideRagdollPoint (with the pelvis sweeping through the pond's surface), CollideRagdollBody,
    SettleRagdoll, AddRagdollPush and AddRagdollHold, ApplyRagdollPushes, Update and ReleaseRagdollContacts (frames
    moved in x, y, z), PlayRagdollSound and UpdateRagdollSound;
  - **trajectories:** 300 of 30 StepRagdoll calls each (9,000), each call from the port's state;
  - **the pose:** RagdollBoneMatrices, SetRagdollBones, PoseFromRagdoll, Store, Reset (from a moved blend start)
    and BlendRagdollToRest, PlaceRagdollPoint, MeasureRagdollBody, AddRagdollBone; Save and Load of the parts and
    the points (the files byte for byte, some points on named contact frames; loaded into a ragdoll whose links and
    quaternions were scrambled first);
  - **the build:** 1,000 BuildRagdoll and frees per side from a bent skeleton and a moved, turned model, compared
    in a canonical form (pointers as indices; quaternions within 5e-3, either sign).
- **New in the harness:** the shared-word comparison takes a tolerance per word; a side hook (the pose's owner,
  movement and vehicle are each side's own objects); a soft mode for the trajectories; words of the weapon pose left
  out by offset; the original's _aligned_malloc and _aligned_free redirected for the build; its car, vehicle impulse
  and settled blood pool calls logged.
- **Port bugs found and fixed (on main; port mistakes):**
  - ClampLimitAxis took the square root of 1 - value² and tested it against the limit: a value past 1 (after an
    earlier axis's scaling) gave a NaN, which spread through the part. The original's x87 test takes the safe branch
    for the unordered result; the port now tests `!(s > 1e-8)` the same way.
  - CollideRagdollPoint swept from the point's position before the last integration step (oldPos); the original
    sweeps from where it was at the last collision step (lastPos, +0x100).
- **Undefined in the original, not compared:** the w of the vectors the original copies in from stack temps (the
  parts' savedStart, savedEnd, limits, axis, savedAxis, unknown220; the points' vectors; the pushes' forces;
  m_gravity.w after the water; the world matrix's m[3][3] after PlaceRagdollPoint, whose children get a temp with no
  w).
- **Tolerances:** the points' velocities, steps and smoothed velocities are differences of positions over a 2..10 ms
  substep (1e-2); StepRagdoll must agree in 99 % of its calls (8,952 of 9,000: float rounding grows chaotically over
  the substeps' collisions; each call starts from the port's state, so differences do not pile up).
- **Results:** every function agrees (20,501 checks with -DRAGDOLL_ONLY, 67,501 with the base's; 0 failures). Lines:
  human_pose_physics.cpp 95.3 % of 827, human_pose_ragdoll.cpp 98.3 % of 718.
  `human/mutations/mutate_ragdoll1.py` (the physics): 746 mutants, 572 killed, 42 not building, 132 survived (the
  car pushes, equivalent ones, paths only the soft trajectories meet, boundaries; listed). `mutate_ragdoll2.py` (the
  build, the pose, saves): 698 mutants, 595 killed, 28 not building, 75 survived (first values set again, frees,
  equivalent ones; listed).
- **Not exercised:** the car pushes (no car actor in the test world); human_pose_ik.cpp (linked only); the
  corrections' fallbacks (unreachable); BuildRagdoll with `reload` (freeing an old ragdoll).
- **Harness lessons (my mistakes):**
  - the first skip of undefined w words also hid the matrices' fourth columns; narrowed, it showed PlaceRagdollPoint's
    undefined world m[3][3];
  - the parts' skip took every 16th word in +0x160..0x22F, scalars among them: thickness (+0x18C) and two more were
    never compared, in the build or the calls, until mutation survivors (the parts' thickness factors) pointed at it;
  - mutation labels are line numbers from when the file was generated: after my two fixes added lines, I tried a
    different line than the label meant by hand (the runner was right);
  - 12-byte out-vectors passed to the original's sphere test, which writes 16 (a crash), and a stdcall helper called
    as thiscall, in a debug build.

## Human part 6 (the movement, `human_movement.cpp`, 2026-11-11)

- **The build:** `human/build_move.bat` (HUMAN_MOVE): build_base.bat's harness with game/human_movement.cpp linked,
  `human/move_test.inc`'s tests alone. Its calls into the movement's parts (the idle, the visual and its lighting, the
  animation player, the special moves, the mixer, the morph, the work, the weapon pose, the equipment) and into the
  game (the shot manager, the surface marks, the sound module, the camera, the actors) are logged on both sides alike.
  The other files' calls into the sound bank, InitBase and DoneBase go to loggers through the linker's --wrap.
- **The tests:**
  - 6a, the helpers, exact: S_HumanAnimRef::Normalize, RotateAboutX / Y / Z, AngleBetween2D, the camera under water
    (20,000 each); and IsLowered, GetHeadWater, PlayDubbing, Unknown7865c0, UseObject, SetUnknown94,
    UpdateModelNames, OnRemoteAction, SetPlayerControlled, AddBloodPool, Ragdoll, OnModelChanged (all 25 joints
    relinked), ResetToFrame (30,000 calls);
  - 6b, the updates: UpdateAnimation, UpdatePhysics, UpdatePhysicsParts (9,000 calls: still, frozen, detached,
    hidden, on a ladder, on dynamic ground, timers running out, a carried body, the head under water, remote humans
    turning);
  - 6c, the lifetime: SetOwner, Init (the mission's name: its climate, footprints and variant; the first human's
    setup, the driver state in network games), Done (the last one's), Reset (6,000 calls; the globals compared);
  - 6d, saved games: SaveGame (the files byte for byte) and LoadGame with AfterLoad (3,000 each), running the movement
    base's SaveBase and LoadBase, untested until now; the parts' saves failing;
  - 6e, the sound bank: 20,000 Play and Return on the port's bank and the original's (its decks, caches, the files it
    opens, the sounds it makes and their settings); the base's UpdateBreath under water with a backpack (6,000).
- **New in the harness:** the remote actions rebuilt as the original's MSVC6 deque (blocks of 341, a map of 8) from a
  snapshot of the port's queue taken before its call, and compared whole after it (ProcessRemoteActions with actions
  runs for the first time); the sound vector's snapshot taken the same way; the original's sound bank entries
  restored for 6e (their first five bytes); a fake backpack; the scene's FindFrame; the profile times not compared.
- **Port bugs found and fixed (on main; port mistakes):**
  - AngleBetween2D rounded each atan2 to float; the original subtracts the two FPATANs at the x87's precision and rounds
    the difference (1 ulp off);
  - Ragdoll (the light and sector update) looked the sector up at the model's position; the original 0.5 m above it.
- **Undefined in the original, not compared:** the w of m_hitOffsets (UpdateHitSpheres), m_unknown240 (ResetBase),
  m_unknown940 and m_unknown9D0 (LoadBase: 12 bytes read, 16 copied), all from stack temps. SaveBase's static spheres
  keep an earlier save's leftovers in the original (cleared before each original save; the port writes zeros).
- **The port's documented fixes, kept out:** a held ladder animation (the original passes the mixer an uninitialised
  vector); the sound group without files (16: the original reads through a null deck; the port's side is tested
  alone).
- **Results:** every function agrees (222,354 checks, 0 failures). Lines: human_movement.cpp 94 % of 784;
  human_movement_base.cpp 61 % in this build (its own tests are parts 4a-4c).
- **Mutations** (`human/mutations/mutate_move1.py`): 682 mutants; 549 killed, 45 not building, 88 survived (classified
  in its docstring: the profile clock, the constructors and destructors, globals' first values, equivalents, the
  fakes' blind spots and a few boundaries not reached). 119 survived the first run; the seats, burnt-out cars, hidden
  models, the head under water and foreign sounds took it to 92, the model frame's flags and the work's changed
  word (now compared / varied) to 88.
- **Not exercised:** GetAnimLength (the animation tables are not linked), GetSectorFrame and SetModelVisible (no
  original address); the movement's and the sound bank's constructors and destructors (not compared with the
  original's); SaveGame failing part way; the copy protection failing (its tables are zero in the test's image:
  intact).
- **Harness lessons (my mistakes):**
  - the queue and the sound vector live on the heap: restoring the movement's bytes does not undo what the port's call
    did to them (the original first ran on counted-down actions; part 4c's lesson, again);
  - crash addresses are printed relocated (rel = address - base + 0x20000000), the original's too: 0x00a1822c was
    0x0078822c, the original's inline IsFirstPersonView reading the game's +0x5C;
  - the camera-under-water flag: the real IsCameraUnderWater reads the port's global, the original's from the script;
  - SaveBase's static buffer is 0x80 bytes, not 0x50 (I cleared too little);
  - the equipment fake answered places 0..3 and RandomMove wrote place 9 past its table;
  - the model frame is shared by both sides and was not restored: the port showed a hidden model and the original
    found it already shown (mutation testing found it);
  - in a few Python edits through the shell, `\n` in C++ strings became real line breaks again (fixed by hand).

## Human part 7 (the animation mixer, `human_anim_mixer.cpp`, 2026-11-12)

- **The build:** `human/build_mixer.bat` (HUMAN_MIXER): build_move.bat's harness with game/human_anim_mixer.cpp
  linked, `human/mixer_test.inc`'s tests alone. The mixer is the movement's field at 0x260 (944 bytes), compared
  whole; the original's methods get the original movement's mixer.
- **The tests:**
  - 60,000 calls over 30 entry points (Init, the stage and track setters and getters, PlayAnim / Carry / Activity,
    the queues, SyncTrackTime, the blends, UpdateStages, Update with its moves, slides and extra moves, AddMove /
    MoveTo / AddMove2 / Slide / SetVelocity / ReplaceVelocity, HoldTrack, FadeOutTrack3, SetHoldTime,
    NotifyFrames), and 20,000 of MixerBlendProgress;
  - the notes: NoteHook and OnNote over every kind of note (dubbing, sound files, groups, steps, holds, events),
    with a note frame "a" under the pelvis; Attach / DetachModelUpdateHook;
  - saved games: SaveGame (the files byte for byte) and LoadGame from the port's file, sometimes with an unknown chunk
    first.
- **New in the harness:** a fake model's 8 stages per model (two models: the human's and a second one with "a"
  frames), shared by both sides; fake animation sets (names, end times, play modes, one sometimes empty) and the
  resource cache's Get redirected to the port's; `RestoreOriginal` (original_exe.cpp) puts back an original function
  that an earlier redirect replaced (the mixer's own, faked for the movement's tests); the original's static sscanf
  and atoi redirected to the test's CRT (their locks were never set up: they hung); the model's callback stored
  canonically, so the original's hook compares as the port's.
- **Port bugs found and fixed (on main; port mistakes):**
  - UpdateStages added the step to a track's time in double; the original's FIADD adds it exactly and rounds the sum
    to float once (near the 0x7FFFFC17 guard the time came out on the other side of it);
  - SyncTrackTime computed the share in double; the original's FIDIV / FIMUL round to float at each step (1 ms off).
- **Undefined in the original, not compared:** the w of velocityA / B, m_move and m_addMove (stack temps' w); LoadGame
  of a stage without its mode chunk (the original reads a stack word; SaveGame always writes the chunk).
- **The port's documented fixes, kept out:** the 5-event cap on a track; HoldTrack of a track without a current
  stage (the original writes stage -1's rate); a saved stage 8; a cached animation file no longer found.
- **Results:** every function agrees (268,000 checks, 0 failures). Lines: human_anim_mixer.cpp 99.6 % of 1,065.
- **Mutations** (`human/mutations/mutate_mixer1.py`): 865 mutants; 750 killed, 49 not building, 66 survived
  (classified in its docstring: equivalents, the documented fixes, AddTrackEvent and float boundaries). 76 survived
  the first run; biasing the random state to the integer boundaries (a hold of 1, a fade-out of 0, stage times at an
  animation's end, a 1 ms animation) took it to 66.
- **Not exercised:** AddTrackEvent (no original address: inline in the animation player); LoadGame's stage-8 fix
  (line 1463); a stage saved without an animation's name.
- **Harness lessons (my mistakes):**
  - the first comparisons failed at call 0: earlier tests had redirected the original's own mixer functions, which
    the original now had to run (hence RestoreOriginal);
  - the original's sscanf / atoi hung the note tests before I redirected them; a crafted save without the mode chunk
    was compared until I saw the original reads a stack word there;
  - I edited the tree while a mutation run was building from it (every mutant "failed to build"; rerun), and one
    edit script went through a shell heredoc against the standing rule (it came through intact).

## Human part 8 (the animation player, `human_anim_player.cpp`, 2026-11-12)

- **The build:** `human/build_player.bat` (HUMAN_PLAYER): build_mixer.bat's harness with the player compiled inside
  `human/player_statics.cpp`, `human/player_test.inc`'s tests alone. The player is the movement's field at 0xC00
  (176 bytes), compared whole; the original's methods get the original movement's player.
- **The tests:**
  - 8a: 40,000 calls of Reset, Init, FindHeadDummy, ReturnToStand, PlayAnimation and the three Adjust*Request
    (thiscall with pointers in the original); 4,000 SaveGame (the files byte for byte) and 4,000 LoadGame, sometimes
    of a crafted chunk with the vectors;
  - 8b: 60,000 starts: OnAnimStarted (with a vehicle and its door handle "klika", the ladder in use with its ends
    one above the other or apart, a body to pick up), OnCarryStarted, OnActivityStarted (the props copied through
    the driver's CreateFrame and Duplicate);
  - 8c: 80,000 updates: Update and its UpdateAnimTrack / UpdateCarryTrack / UpdateActivityTrack, over the real mixer.
- **New in the harness:**
  - the player's update statics (27, in an anonymous namespace) are reached by compiling the player inside
    player_statics.cpp, whose table gives each one's port address, size and original address. DiffPlayer copies the
    port's statics to the original's before its call and compares them after (S_HumanAnimRef 13 of 16 bytes,
    S_MixerAnim 17 of 20: padding);
  - the animation tables are one set of byte buffers in the original's layout, named by both sides' globals;
  - mutrun.py's MUT_WRAPPER: a mutant rebuilds the wrapper (player_statics.cpp) in its object's place;
  - g_skipWords skips single words of a compared field (LoadGame's vector w);
  - the work, equipment, weapon pose and first-person view calls logged on both sides; SetCarryMode logs the work's
    m_forced and SetModel the equipment's hand dummy flag (the player sets both around those calls).
- **Undefined in the original, not compared:** the w of m_unknown70 / m_unknown90 after LoadGame and OnAnimStarted
  (stack temps).
- **The port's documented fixes, kept out:** no animation table, or a work group other than 1 / 2; a door animation
  (0..3) without a vehicle; picking up a body frame without an actor; the mixer's ReleaseTrack of a track without a
  current stage; the locomotion's ladder velocity (0x6A with track 0 held, or no ladder in use).
- **Inputs outside the tables, kept out (both sides read past them):** activities from 75, carry indices from 273,
  a 4th stance.
- **Results:** every function agrees (572,000 checks, 0 failures). No port bugs found. Lines:
  human_anim_player.cpp 99.2 % of 1,443.
- **Mutations** (`human/mutations/mutate_player1.py`): 1,269 mutants; 1,114 killed, 74 not building, 81 survived
  (classified in its docstring: mostly equivalents; the no-table fix; the items). 116 survived the quick pass and 103
  at full strength. Clearing Init's pointers first, stale statics for Update, model-part frames on the arm joints,
  the logged m_forced / hand dummy flag, models in the equipment's places 6..8, the ladder's geometry and a few
  boundary biases took it to 81.
- **Not exercised:** the items: PlayItemSound with an item (lines 177-187), the grenade's take sound, the belt item's
  frame for activity 0x41 (line 1800). The harness gives the human no items. Also the working frame's AddRef (the
  fake frames keep no count).
- **Harness lessons (my mistakes):**
  - test code appended through a bash heredoc turned `"anims\\run.i3d"` into `"anims\run.i3d"`, against the standing
    rule; I fixed it with Edit;
  - a build failure (a duplicate RandomUpdate) went unseen, and a run used the stale exe;
  - my first crafted LoadGame chunk held a 4-byte vector where the original reads 12: malformed input, not a
    difference.

## Human part 9 (the special moves, `human_special_moves.cpp`, 2026-11-12)

- **The build:** `human/build_special.bat` (HUMAN_SPECIAL): build_player.bat's harness with the special moves linked,
  `human/special_test.inc`'s tests alone, in part 1's world. The movement base, the mixer and the animation player are
  the real ones on both sides; the original's special moves, which parts 1 and 6 redirected to loggers, run again
  (RestoreOriginal). Their thiscall entries take pointers (CanTurn's angle too).
- **The tests (222,000 checks):**
  - 30,000 small calls: Init, CanTurn (swimming, leaning, lying; on the ramps), AdjustLean, StartSitting,
    PushOutOfWalls (before obstacles), UpdateWater (the ponds);
  - 12,000 ladder calls: UseLadder, EnterLadderFromTop, CanEnterLadderFromBottom, at the foot and the top, the ladder
    sometimes moved against the 3 m platform, swimming now and then;
  - 16,000 TryClimbOver before every obstacle (steps, walls, platforms, the truck, blocks under ceilings), standing or
    not, from a ladder;
  - 24,000 deaths: HasRoomToFall, and Die for every side, drowning, the special hits, a backstab with its killer,
    seats and seat weapons, the stances and the swimming, with the death seed and the last seat death synced;
  - 21,000 HasRoomForStance over the seven modes, moving or not, under the low roof.
- **New in the harness:**
  - part 9's world only (`#ifdef HUMAN_SPECIAL` in LoadGround): a shallow pond, a pond over a sloping floor (every
    depth), blocks under thick slabs with undersides (room to climb crouching), two blocks and an underside for the
    low roof. The other parts' world is unchanged;
  - the original's static initialisers of the lying head / feet offsets (0x0074a110..0x0074a170) run in setup: the
    test's copy of the image never ran them, and lying down and getting up differed on 2,574 of 21,000 calls until
    then;
  - loggers for DropCarried, the ladder's AddClimber, the vehicle's LeaveSeat (slot 0x80 / the port's own);
  - `g_workSkipWords` (words of the work left out); the effects module's catch-all prints its caller.
- **The port's documented fix, compared as fixed:** PushOutOfWalls on a hit puts the body's collision back; the
  original's side gets the same restore after its call (RestoreLikeThePort).
- **Undefined in the original, not compared:** the w of the look-at vector the climbs and the ladders clear (the
  work's +0x1C) and of the trail spheres they place.
- **Kept out:** a carried body with its actor in Die (it goes through the effects module and the body's own movement).
- **Results:** every function agrees (222,000 checks, 0 failures). No port bugs found. Lines: human_special_moves.cpp
  974 of 1,040 (93.7 %). Not run: Die's side deaths and its crouching tied-up death, push state 5 in HasRoomToFall, a
  zero lean reach, room to climb standing under something (all unreachable on both sides); the blocked ladder entries
  (their filter excludes every flagged volume); a static weapon to climb over; a stance outside 0..3 for a death.
- **Mutations** (`human/mutations/mutate_special1.py`): 907 mutants; 704 killed, 64 not building, 139 survived
  (classified in its docstring: unreachable code, equivalents, what this world cannot reach, float boundaries). 164
  survived the quick pass; the sloping pond, slopes, a killer, Die's work blocks and trail, the model moved since its
  dummies, stance volumes for another stance and the low roof's underside took it to 139.
- **Harness lessons (my mistakes):**
  - inputs the game never makes crashed the port (and would the original): a ladder state with no ladder, seat-weapon
    modes with no vehicle; a dead push state and a zero volume scale from part 3/4's random state left the body
    without stance volumes, so the climb tests tested nothing near the new blocks until I looked;
  - the killer I placed was the port's object only: the original reads its own copy (the pointer is mapped);
  - my first blocks under the ceilings were too low to climb and the slabs had no underside (AddBlock makes none):
    found by a probe, not by reasoning;
  - two more edits went through a bash heredoc against the standing rule (one turned `\n` into a line break; fixed
    with Edit).

## Human part 10 (the idle and the interactions, `human_idle.cpp`, 2026-11-12)

- **The build:** `human/build_idle.bat` (HUMAN_IDLE): build_special.bat's harness with the idle linked,
  `human/idle_test.inc`'s tests alone. Everything under it is real on both sides (the movement base, the mixer, the
  animation player, the special moves); the original's idle functions part 6 redirected run again.
- **The tests (132,400 checks):**
  - 20,000 small calls: Reset (and the statics), Update: standing fidgets, the swimming bob at and under the surface,
    the breath vapour of cold missions (moving and not, first person, faded out);
  - 40,000 calls of Interact (every interaction code, with and without the body flag: doors, the seats of a car,
    chairs with and without an exit dummy, healing, manipulating, tying up, cutting, backstabs, their ends with the
    remote steps at the front of the queue and full queues) and UpdateInteraction (every interaction under way, its
    timers, the seat's flags and occupant, getting in and out, sitting down and getting up);
  - 4,000 saved games (the files compared) and loads, with made-up chunks and nothing open.
- **New in the harness:**
  - the shared car gets one vtable per side, the original's swapped in for its call: the port's virtual slots sit 4
    bytes after the original's (g++'s two destructor entries), so one table cannot serve both (the port's LeaveSeat is
    the original's ExitSeat slot);
  - part 8's animation tables answer GetAnimLength (part 6's fakes answered none: the doors' times differed);
  - the original's call runs WithOriginalBody, with the props it copies named on its own count (the player's Update
    runs inside UpdateInteraction);
  - loggers for the door's side, the vehicle's seats, RemoveShadow, the weapon pose's seat, the work's ApplyHit, the
    team's camera target; GetMoveMode; SetSeatStance's body (inline in the original);
  - SetFacingDir's loggers record the work's m_forced, and DiffCall compares m_forced's byte exactly: a bool in the
    low byte of a word whose upper bytes look like a float passed the word's float tolerance (all parts rerun: 0
    failures).
- **Port bugs found and fixed (on main; port mistakes):**
  - being tied up: the body turned to face the captor; the original turns it away (owner minus target, as for the
    backstab);
  - the mixer's track time past INT_MAX: the original's __ftol converts in 64 bits and keeps the low word (a wrapped
    time); the port's conversion gave 0x80000000. Only reachable with a track within 2,800 of INT_MAX (about 17 days
    of one animation), but a difference;
  - the animation player asked for the hands weapon's sound in the first person view, where the original does not
    ask for the weapon at all (a getter's call; part 8's tests had not met the first person there).
- **Not compared:** GetSeatOccupant's calls (a getter: the original calls it up to three times where the port calls
  it once).
- **Results:** every function agrees (132,400 checks, 0 failures). Lines: human_idle.cpp 749 of 762 (98.3 %). Not
  run: a living human in the seat being taken (another human is not part of the harness; a dead occupant is), the
  wire cutters and a seat weapon's definition (no items), the model outside the primary sector.
- **Mutations** (`human/mutations/mutate_idle1.py`): 490 mutants; 405 killed, 39 not building, 46 survived
  (classified in its docstring). 53 survived the first run.
- **Harness lessons (my mistakes):**
  - random states the game never makes crashed both sides: getting into a seat without a vehicle, a chair's actor
    as the car to get out of, and the body itself alive in the seat it takes (Interact recursing for ever);
  - human_anim_mixer.cpp and human_anim_player.cpp changed here: mutate_mixer1.py's and mutate_player1.py's labels
    after the changed lines are a line off (they match by text).

## Human part 11 (the visual, `human_visual.cpp`, 2026-11-12)

- **The build:** `human/build_visual.bat` (HUMAN_VISUAL): build_idle.bat's harness with the visual linked,
  `human/visual_test.inc`'s tests alone. The lighting (C_HumanLighting) stays part 6's loggers (its own part).
- **The tests (116,000 checks):** 40,000 small calls (ResetState, UpdateStandingFlag, SetTarget, Glide,
  UpdatePosition, SetNetworkPosition / Direction, SetPosition / SetDirection with and without a seat, SetNetworkDriven,
  Unknown7585d0, ProcessAnimEvents over every event kind, Update); 16,000 of the model's life (Init, Done, Reset,
  InitModel, DoneModel, RebuildShadow, RemoveShadow) and the saved games (the files compared; loads of made-up chunks).
- **New in the harness:**
  - loggers for ShowHandsItem, Unknown7938f0, UpdateLensFlare, the morph's Update, FitProneBody, the work's
    SetViewDir, the effects module's CreateShadow (slot 0x78 / the port's);
  - the ladder's step sound and frame on both sides' copies; the object of place 7 and the shadow frame restored for
    the original's call and their state compared (references, flags, links, the shadow's +0x1FC);
  - call arguments that are whole numbers on both sides are compared exactly: the float tolerance passed 10000
    against 10001 (all parts rerun: 0 failures).
- **Port bugs found and fixed (on main; port mistakes):**
  - Update looked the model's sector up at the model's position; the original looks half a metre above it (a body
    standing at a sector boundary by the floor got the other sector);
  - SetNetworkPosition passed the target on with w 0; the original copies all four words.
- **Also fixed:** human_equipment.h said UpdateLensFlare takes its argument in EDI; it is an ordinary stack argument
  (RET 4). My first logger followed the comment and unbalanced the original's stack.
- **Undefined in the original, not compared:** the w of m_unknown70 (ResetState), of m_glideTo / m_step (SetTarget's
  on-along path), of the targets LoadGame reads (12 bytes copied as 16); the crossfade of track 0's animation in Init
  and Reset (a stack byte the original never sets; its weight logged and its stages' crossfade skipped in those two
  tests).
- **Results:** every function agrees (116,000 checks, 0 failures). Lines: human_visual.cpp 713 of 726 (98.2 %). Not
  run: a carried body's own lighting (another human), a seat-mode branch under another interaction, the length
  helper's exact squares, the weapon's drawing with its blend done (reached before the inputs changed).
- **Mutations** (`human/mutations/mutate_visual1.py`): 527 mutants; 441 killed, 32 not building, 54 survived
  (classified in its docstring). 67 survived the first run.
- **Harness lessons (my mistakes):**
  - a variable named `near` (windows.h makes it nothing), against my own note;
  - the UpdateLensFlare logger above; ladder state set on one side only; a stray no-op DiffMove left in a test.

## Human part 12 (the lighting, `human_lighting.cpp`, 2026-11-12)

- **The build:** `human/build_lighting.bat` (HUMAN_LIGHTING): build_visual.bat's harness with the lighting linked,
  `human/lighting_test.inc`'s tests alone (the lighting at the movement's +0xDA4).
- **The tests (60,000 checks):** Init, Update (every mode, the model drawn in the last frame or not, long steps),
  Sample (the controller's values or the game's measure, references of 0, the climate), Approach, SetSector over a
  fake sector's lights (on, ambient or not, colors with each channel strongest and two alike).
- **New in the harness:** loggers for the game's GetLightIntensity (with the model's on flag: the measure hides it),
  the human's ControllerUnknown5C, the effects module's SetModelLightLevel (slot 0x54 / the port's); a fake sector
  with lights and a model with its render stamp and frame data.
- **Results:** every function agrees (60,000 checks, 0 failures); no port bugs. Lines: all 85.
- **Mutations** (`human/mutations/mutate_lighting1.py`): 87 mutants; 70 killed, 7 not building, 10 survived
  (boundaries, mode 3's long interval, an uncounted Update, an equivalent first value). 15 survived the first run.
- **Harness lessons (my mistake):** one more edit script through a bash heredoc with backslashes (it half applied;
  rewritten with the Write tool).

## Human part 13 (the morph, `human_morph.cpp`, 2026-11-12)

- **The build:** `human/build_morph.bat` (HUMAN_MORPH): build_lighting.bat's harness with the morph compiled inside
  `human/morph_statics.cpp` (its tables of Tables\morph.sav are file-local: the wrapper gives their places),
  `human/morph_test.inc`'s tests alone (the morph at the movement's +0x610).
- **The tests (86,666 checks):** Init and Relink, Done, Reset, SetBlinking, SetUnknown1..3, SetExhausted, ShowDeathFace
  (the camera near and far, the eyes turned), Apply (forced or by the blends under way), Update (blinks, panting,
  expressions, grips, their ends), UpdateBreathSound (the active player, holding breath, the sound's length and
  volume), the saved games (the files compared; made-up chunks).
- **New in the harness:**
  - both sides' morph tables filled with the same random weights, valid flags and channel flags;
  - the original's static initialisers of the morph's times (0x00780cb0..0x00780dd0) run in setup;
  - a morph visual under the model ('SMMR' through its own vtable copy) with its morph interface at +0x120 (COM,
    stdcall): the weights set logged; the eyes (Leye / Reye) restored for the original's call and their matrices
    compared; loggers for GetStaminaPercent and IsPlayerHoldingBreath.
- **Kept out:** ShowDeathFace and Apply without a morph visual (the port's documented fix: the original reads near
  null).
- **Results:** every function agrees (86,666 checks, 0 failures); no port bugs. Lines: human_morph.cpp 484 of 491
  (98.6 %). Not run: loading morph.sav (the tables are filled by the test), Init without a model, a model without eyes.
- **Mutations** (`human/mutations/mutate_morph1.py`): 435 mutants; 378 killed, 25 not building, 32 survived (classified
  in its docstring). 50 survived the first run.
- **Harness lessons (my mistakes):** a no-op placeholder DiffMove written into a test case again (replaced before it
  ran); clearing the owner before Relink crashed both sides (Relink calls Init with it).

## Human part 14 (the work, `human_work.cpp`, 2026-11-13)

- **The build:** `human/build_work.bat` (HUMAN_WORK): build_morph.bat's harness with the work linked,
  `human/work_test.inc`'s tests alone. Everything the work reaches is real on both sides now (the movement, the mixer,
  the animation player, the special moves, the idle, the visual, the lighting, the morph); the original's work, which
  parts 1-11 redirected to loggers, runs again. The work's words are compared after every call (o_work).
- **The tests (130,434 checks):** every entry, 23 in all: SetMovement, SetGroup, Reset, Update (deaths through the
  special moves, the special moves' climbs and sitting, stance and gait changes, the carry animations of every action
  with empty and full hands, the pick-up and carrying states, hit reactions, interactions passed on to the idle),
  SetStanceTimer, UpdateStanceTimer, GetViewDir, Play (every work mode: ladders, seats, swimming), PlayWorkAnimation,
  SetStance, SetGait, SetLookAt, SetCarryMode, DoAction, SetUnknown5C, SetFacingDir, SetViewDir, SetWorkingFrame / 2,
  ApplyHit, Interact, SetLook, the saved games (the files compared; made-up chunks).
- **New in the harness:** the work's fakes of parts 1-11 off in this build; loggers for the weapon pose's SetViewDir,
  OnHit and SetLook, the first person view's SetPose, the equipment's GetHandsProjectileKind; the locomotion's and the
  special moves' links set (part 3's Init test leaves the locomotion's cleared); the working frames' references
  compared.
- **Undefined in the original, not compared:** the facing's w (+0x2C) and the pads after the hit's force and position
  (+0x7C, +0x8C): the original copies the 4 bytes after the vectors it is given (the port's documented fixes).
- **Kept out:** a morph without its visual in a death (part 13's documented fix).
- **Results:** every function agrees (130,434 checks, 0 failures); no port bugs. Lines: human_work.cpp 629 of 639
  (98.4 %). Not run: a remote human taking its hands item (no items).
- **Mutations** (`human/mutations/mutate_work1.py`): 419 mutants; 363 killed, 28 not building, 28 survived (classified
  in its docstring). 30 survived the first run.
- **Harness lessons (my mistakes):**
  - the death's seed was not synced in these tests: the port's is clock() % 100 at start-up, the original's 0, so the
    dying animations agreed only when the start-up was quick. The slower coverage build showed 98 differences; the
    tests now set both and check them after each update;
  - my random state copied the request into the movement's record whole, so the update always dropped the action
    and the actions with a hands item never ran until coverage showed it;
  - the first mutation run was started with the morph's MUT_WRAPPER still set, which rebuilds the wrapper instead of
    the mutated file (every mutant would have "survived"); stopped and rerun without it.

## Human part 15 (the weapon pose's skeleton and chains, `human_pose_ik.cpp`, 2026-11-13)

- **The build:** `human/build_ik.bat` (HUMAN_IK): build_base.bat's harness, `human/ik_test.inc`'s tests alone. A model of
  23 jointed bone frames, each under its parent bone as the human models have them (the ragdoll's standing places),
  bound the way BindSkeleton binds it (the port's AddBone, SetLimitAxes, SetLimitStiffness, PoseAimZ; BindSkeleton itself
  is part 19's). The pose is shared and compared word by word; the bone frames' world and local matrices, rotations,
  scales and flags too.
- **The tests (63,005 checks):**
  - the matrix helpers from random input (24,000): PoseTransposed, TurnedAround, Orthonormalize, AimZ (with an axis
    exactly along the direction, a skewed matrix), FrameFromZ (straight up / down), FrameFromZ rolled, FrameFromZX,
    RotationBetween (nearly parallel and opposite: the reflection), TurnZTo / TurnYTo, GetRoll, SlerpDir (the dot past
    1, parallel, opposite);
  - the skeleton (24,000): AddBone (out of range, a parent's three children taken), SetLimitAxes, SetLimitStiffness,
    AimBone (out of range, the pelvis under the model, weights negative, -0, 0..1), SetHandRotations (null hands, other
    frames), SetHandMatrices, BendJoint (any bone, the joint on its limb's line, the direction along it), ClearChains;
  - the chains (40,000 rounds): BuildChain (no effectors), RefreshChain, SolveChain (a pass), ApplyChain, from random
    chains as the seats make them (the spine to the hands and the neck) and others (other roots, the feet, the head,
    the elbows; weights; stiffness over 1; limits changed), the model somewhere and turned, the bones bent, the world
    matrices up to date or not, the pelvis locked; whole solves one pass at a time from the port's state; the seats'
    sequence over three updates.
- **Judged by their rates (the float rounding grows):** single passes 99.95 % (71,954 of 71,955; 2 or 3 in 72,000
  differ by 3e-4..1.3e-3 in a word, not traced: presumably ill-conditioned passes), the seats' sequence 99 %, whole
  16-pass solves 97 % and three of them 88 % (0 differences after one pass in 6,667 solves, 2 after two, 36 after five,
  150 after sixteen: the rounding grows with the passes).
- **Undefined in the original, not compared:** the solved matrices' m[3][3] (ComputeSolved's own-joint branch copies
  the tip into row 3 as 16 bytes from a stack temp whose w it never stores, 0x007a6634); the frames' scale w (+0xDC: the
  inline SetLocalMatrix copies GetScale's 12-byte result as 16 bytes).
- **Port fixes (on main):**
  - SolveChain's comment claimed the original returns `this` after moving the pelvis: it returns the passes, as the
    port does (comment corrected);
  - the SDK's "below 1e-8" test is an integer compare of the float's bits in the original (CMP ...,0x322bcc77): a
    negative NaN counts as below; and a NaN takes the square root in its unit test, then fails the length test. The
    port's float compares sent a NaN the other way in BendJoint (twice) and SolveChainBone (found by part 16's tests,
    where a NaN aim reached the same test in TurnBone): PoseBelowEpsilon and NaN-aware compares now.
- **Results:** every function agrees; lines: human_pose_ik.cpp 99.1 % of 545. Not run: the length tests after a square
  root of at least 1e-8 (unreachable but for a positive NaN), LinkChain's dead ends (a pin's bone always in the tree).
- **Mutations** (`human/mutations/mutate_ik1.py`): 520 mutants; 407 killed, 48 not building, 65 survived (equivalent
  ones, exact float boundaries; classified in its docstring).
- **Harness lessons (my mistakes):**
  - the original's PoseSlerpDir reads and writes whole S_vector4s; my first test passed a 12-byte output, which the
    original overran into the port's result next to it at -O0 (the coverage build's 1,669 failures);
  - I wrote two edits through bash heredocs with backslashes again (a `\n` became a line break; caught at once).

## Human part 16 (the weapon in the hands and its holds, `human_pose_weapon.cpp`, `human_pose_hold.cpp`, 2026-11-13)

- **The build:** `human/build_weapon.bat` (HUMAN_WEAPON): build_ik.bat's harness with the two files linked,
  `human/weapon_test.inc`'s tests alone. On part 15's skeleton: the hand dummies hw1 / hw2 under the hands, a third
  person camera dummy, six weapon models (grips "LW" / "RW" and a stock "BUTT" or not), animation sets for the arms'
  activities (FindTrack, the position and rotation keys logged), the frames' SetWorldDir (slot 0x1C, logged). The pose's
  owner is each side's own human; the frames' matrices, flags, links and reference counts are compared.
- **The tests (87,006 checks):**
  - the arms (30,000): SolveTwoBone (reachable or not, hints, the joint compared), TurnBone, TurnBoneAbout, AimWeapon
    (the hands, the placing, the stock to the shoulder), ReachArm (left / right, hints);
  - the modes (40,000): InitHands, SetModel (any weapon, none, the same), SetMode (0, 1, 2, others), SetSupportHand,
    RaiseWeapon, LowerWeapon, PauseWeapon, PauseHands, SetViewDir (the same and the opposite view), StartRecoil,
    PlayActivity, ApplyAnimation (bones without tracks, keys at the same time, one or no key), UpdateActivity;
  - the holds (48,000): UpdateGrip, UpdateRecoil, and judged by their rates (99.5 %; they agree in 99.65..100 %):
    UpdateHold, UpdateFree, HoldStanding, HoldCrouching, HoldSeated, HoldProne, from states with every stance, speeds
    from still to running, pistols and rifles, the support hand, aims anywhere (straight up, down and back too), no owner.
    The holds chain a dozen turns and slerps and some states are ill-conditioned: moving the aim by 1e-7 alone moves the
    port's own result past the tolerance in about 0.2 % of them (`WEAPON_SENS`), the rate the sides differ at.
- **Undefined in the original, not compared:** the w of the hand targets, the weapon's direction, the aim and the hands'
  offsets (stack temps copied as 16 bytes); the weapon frames' m[3][3] (AimWeapon's row 3 from a temp, 0x007c0d94: the
  port's 1); the prone normals' w. Hand values 2 and 3 stay out of the holds: SetMode never sets them, and HoldProne's
  direction starts undefined for them in the original.
- **Port fixes (on main):**
  - SetViewDir summed its dot product x first; the original sums z first (0x007b9391), and the rounding decides its
    "at once" test for views along the aim;
  - the activity time advanced in double; the original rounds the product and the sum to float (0x007b0122): a
    millisecond short in a fifth of the updates;
  - WeightedTurn (TurnBone, TurnBoneAbout) and SolveTwoBone took the SDK's tests as float compares: the original's
    "below 1e-8" is an integer compare and a NaN takes the square root and then fails the length test (a turn of zero
    length with no time passed made a NaN aim: the port turned the bones by it, the original left them);
  - the holds' clamps (ClampAim twice, HoldProne three times) tested `s <= 1e-8`; the original jumps to that branch on
    "<= or unordered" (0x007bd52a): an aim straight up or down gave the port a NaN aim (7.5 % of such holds differed);
  - ClampAim's, HoldProne's and UpdateFree's dot products are summed z first and the speed x first, as the original's.
- **Results:** every function agrees; lines: human_pose_weapon.cpp 99.8 % of 565, human_pose_hold.cpp 98.5 % of 202.
  Not run: WeightedTurn's length test after a square root (a positive NaN only), HandAxis for hands 2 and 3, the
  speed without an owner (its callers test the owner first).
- **Mutations** (`human/mutations/mutate_weapon1.py`, `_weapon2.py`): human_pose_weapon.cpp 537 mutants, 436 killed, 39
  not building, 62 survived; human_pose_hold.cpp 151, 124 killed, 10 not building, 17 survived (equivalent ones, exact
  boundaries, and one that only moves a hold's rate: judging by rates lets a change in a rare branch through; classified
  in their docstrings).
- **Harness lessons (my mistakes):**
  - my rule taking the pose's floats below 1e-6 as equal (added in this part, committed with part 15's harness) read
    small integers, flags and pointers as tiny floats: the stance, the hands, times and the weapon pointers were not
    compared in this part's tests until a full-strength mutation rerun killed none of 147 survivors and I checked one
    by hand. My first fix (exponent of at least 2^-67) still let the harness image's pointers through (they read as
    1e-19); now only words with an exponent of at least 2^-55 count. InitHands' resets also had no random states to
    reset. The tests and the survivors ran again (68 more killed);
  - two more edits through bash heredocs with backslashes (caught by Python's escape warning and a check), and a sed
    edit that turned human_pose_hold.cpp's line endings to LF (put back).

## Human part 17 (the prone body, the head and the seats, `human_pose_body.cpp`, `human_pose_seats.cpp`, 2026-11-13)

- **The build:** `human/build_body.bat` (HUMAN_BODY): build_weapon.bat's harness with the two files linked,
  `human/body_test.inc`'s tests alone. Vehicles: a car with a steering wheel ("LWC" / "RWC") and seat weapon grips
  ("LWS" / "RWS"), a car without the wheel's grips, a static weapon with grips and one without; each a port object
  and an original-layout block (type +0x18, frame +0x20, a car's velocity +0x4A0) with its own vtable (GetSeatCount,
  HasSeatWeapon, GetSeatWeaponAim with its hidden return pointer, GetSeatWeaponLimits; logged), the pose's vehicle
  pointer translated per side; C_Vehicle::SetSteering logged on both sides. The human's model follows the skeleton's
  (its lying dummies give the prone spheres), its stance dummies bound as on every model.
- **The tests (46,005 checks):**
  - FitProneBody (12,000): the skeleton lying along the model (or standing: the fit gives up), the fit unset, settling,
    blending, the tests on odd updates, moved or turned since the last fit, over the test world's ground;
  - the head (40,000): SetLook (every request, a missing point), PickGlance (every side, quick, nervous, once),
    UpdateHeadLook (looking at a point, glancing, returning, the aim's look while holding), TurnHead;
  - the seats (40,000): SetSeat (in and out, every vehicle and seat flag, from the steering), UpdateSteering (the
    wheel turned past 60 degrees either way), UpdateSeat (a seat weapon or only the neck), UpdateStaticWeapon (with
    the gunner poses or without, aims across the weapon's limits, a grip lost), SetSeatWeaponPose.
- **Judged by their rates:** the seats' updates solve 16-pass chains (95 %; they agree in 96.4..99.0 %); UpdateHeadLook
  and FitProneBody (99.9 %; they agree in 99.97 % and 99.99 %). **Not traced:** those head looks all turn the head
  back towards its own direction (LOOK_RETURNING) through the clamps, differing by up to 0.2; the port's TurnHead aimed
  at where the original's head ended up reproduces it in some of them and not in others; the sums' groupings, the
  clamps, the slerp and LS3DF's RotateByMatrix were checked against the original and agree.
- **Undefined in the original, not compared:** the w of the effectors' targets, the accelerations, the look's
  direction, start and end, the prone buffers' vectors; the gunner positions' w.
- **Port fixes (on main):**
  - the clamps of NotSteeper, KeepForward and the look at a point took `s <= 1e-8`; the original takes that branch on
    a NaN too (0x007ad659 and others), and KeepForward returns a NaN direction unclamped;
  - SetLook's and UpdateHeadLook's directions into axes summed x first; the original groups them (y + z) + x, and
    normalises the direction to the point with the square summed z first;
  - Capped (the neck's sway) took the SDK's tests as float compares (the integer "below 1e-8", NaN returned);
  - UpdateStaticWeapon's aim in the weapon's axes summed x first; the original groups (y + z) + x.
- **Results:** lines: human_pose_body.cpp 96.8 % of 412, human_pose_seats.cpp 99.7 % of 315. Not run: NotSteeper and the
  look's clamp for exactly vertical normals, KeepForward's clamps of components past 1 (unit inputs only), the zero
  direction to a point at the head, the velocity of a seat in a vehicle other than a car (the game never has one).
- **Harness limit:** the original's C runtime was never started in the harness: an acos of a value past 1 sets errno,
  whose per-thread block it cannot allocate (a crash in calloc). The static weapon's aims are kept off its up axis by
  1e-5; in the game such an acos returns a NaN and goes on.
- **Mutations** (`human/mutations/mutate_body1.py`, `_body2.py`): human_pose_body.cpp 467 mutants, 333 killed, 31 not
  building, 103 survived; human_pose_seats.cpp 264, 198 killed, 23 not building, 43 survived. Many survivors are paths
  the tests' inputs do not reach (unit directions only, exact boundaries) or that the rate-judged updates absorb; one
  finding: a static weapon's aim in the left half never blends its poses (the turn is clamped to 0 there, in the
  original too), so mutants there change nothing. Classified in their docstrings.
- **Harness lessons (my mistakes):**
  - my first vehicle fakes assumed the port's C_Actor and C_Car have the original's layout (they do not: static
    asserts caught it); then port and original objects per side;
  - random states the game never makes crashed both sides or the port: steering without a vehicle, a body fit without
    an owner, a static weapon's update without its vehicle; the stance dummies left unbound gave both sides garbage
    spheres (uninitialised stack);
  - my scratch copy of human_test.cpp in the scratchpad picked up an old body_test.inc of another session (quoted
    includes search the including file's directory first).

## Human part 18 (the dead body, `human_pose_dead.cpp`, 2026-11-13)

- **The build:** `human/build_dead.bat` (HUMAN_DEAD): build_body.bat's harness with the file linked, `human/dead_test.inc`'s
  tests alone, every source compiled with `-include harness_malloc.h`: the port's _aligned_free goes through the harness
  (and the original's, 0x007e559c, is redirected likewise), which skips the frees while a call is compared, since the
  other side's run from the same restored state still reads the parts, points and slide points dropped (leaked).
  On part 17's skeleton the port builds a ragdoll; its parts, points, pushes and holds are shared and compared, the
  slide points' block too; a first person camera dummy and the "z" dummy; logged on both sides with scripted answers:
  the profile's DETAIL_RAGDOLL (g_gameProfile's storage under its name), the camera module and its camera (slot 0x6C),
  the owner's IsUnconscious, the movement's Ragdoll and AddBloodPool, the stains' AddHit (a fake object), the scene's
  AddHumanModel and MarkTrackedModel; the base's logger of the original's StartDeath taken off.
- **The tests (56,002 checks):**
  - the parts (40,000): ApplyHitImpulse (every zone, parts dropped), AddSettledBloodPool (standing and lying ragdolls,
    contacts), OnHit (dead or not, a settled ragdoll, stains), SampleDeathAnim (collapsed bones), PickUpBody;
  - StartDeath (16,000): every kind, with an animation or none, hit zones with the fall / blast / sink bits, slow and
    fast pushes (from behind, with or without ground ahead), carriers, unconscious bodies, hidden parts, a second death,
    the camera near, far, behind, the ragdoll detail off, small or hidden bodies; the slide points each side allocates
    compared after the call;
  - UpdateDeathAnim (20,000): every kind (a held body's checks falling into the ragdoll, a falling body's slide), the
    animation's end, lying on an actor, the first person camera.
- **Judged by their rates (99.9 %):** StartDeath (15,999 of 16,000: a part's measured rotation just past the tolerance,
  or -0 against a small value) and UpdateDeathAnim (19,998 of 20,000: the slide's speed up to 7 % apart). **Not traced:**
  the latter, presumably two points hitting at nearly the same fraction of the move with the first one picked
  differently; the sums' groupings and the NaN branches are the original's.
- **Undefined in the original, not compared:** the w of the body's place, the slide speed, the camera's place, the
  gravity and the slide points; the slide points' pointer (each side's own block; their values are compared).
- **Port fixes (on main):**
  - a slow push held the pelvis twice; the original holds the two feet, each if it is there (0x007b3372);
  - StartRagdoll asked the owner's IsUnconscious once for the collars' rest; the original asks again (0x007b1a12)
    (the same answer; the calls now match);
  - the slide's hit fraction and its slide off the normal summed x first; the original groups (x + z) + y and
    (z + y) + x (0x007b5a64): the rounding decided which point hit first (8 of 20,000 updates differed);
  - the slide's test of a move along the hit (`along <= 1e-8`) and its clamp of the ground's normal (`s <= 1e-8`) took
    the other branch on a NaN than the original.
- **Results:** lines: human_pose_dead.cpp 98.3 % of 636. Not run: an unconscious body carried off (its ragdoll built again:
  new blocks per side, not comparable here), the contacts let go (part 5's), a held body falling with no ground under
  its knees only, the vertical normal's clamp.
- **Mutations** (`human/mutations/mutate_dead1.py`): 606 mutants; 446 killed, 40 not building, 120 survived: fields set
  again later in the same death, the dropped parts' frees (skipped by the harness), paths not reached (the carried
  unconscious body, a vehicle in the slide's way), equivalent ones, boundaries and the two rate-judged calls
  (classified in its docstring). The first full-strength rerun killed 3 of 130; random values for the ragdoll state a
  death sets (they already held the values it writes), a bent ragdoll and the model's links compared killed 7 more.
- **Harness lessons (my mistakes):**
  - three more backslash edits through bash heredocs (one a `\n` in a printf, caught by the build, two by grep), and a
    sed that turned human_pose_dead.cpp's line endings to LF (put back);
  - my first Write of dead_test.inc did not happen (the stub file had not been read), so a build ran the stub.

## Human part 19 (the weapon pose's core, `human_weapon_pose.cpp`, 2026-11-13)

- **The build:** `human/build_core.bat` (HUMAN_CORE): build_dead.bat's harness with the file linked, `human/core_test.inc`'s
  tests alone. The bone frames answer to the joints' short names ("u", "q", ...) as BindSkeleton finds them. Logged on
  both sides: the stains (a fake object: Bind, SetVisible, the destructor; the port's constructor too), the ragdoll's
  sounds at rest (GetSound, slot 0x78, as in part 7). The profile's DETAIL_STAINS is scripted, on only where the pose
  already has stains (a new one would be each side's own allocation). The animation cache (raw storage under its name)
  answers nothing; these tests do not call it.
- **The tests (12,008 checks; the updates judged by their rates):**
  - BindSkeleton, Attach, Reset, SetStainsVisible (12,000, exact):
    - a new skeleton after Attach's resets (mode 0);
    - the bound skeleton bound again (mode 2: the movement's model change; its chain frames kept through their bone
      numbers);
    - the frames found again after a saved game (mode 1: chain frames as bone numbers, a dead body laid down again);
    - Attach without a model (reload or not), and Reset;
    - each case with cameras or none, stains or none, the detail on or off, and values other than the ones written (the
      bound rest state already held them);
  - Update and Refresh of a living body (24,000):
    - part 16's weapons, modes, blends and timers: raise, lower, pause, the support hand on and off (some running out
      exactly), the kick, an activity;
    - KeepsWeapon letting go, the owner dead, swimming, the prone fit;
    - the cameras kept out of walls, the movement hidden or tiny, Refresh already run this update;
  - in a seat (12,000): steering, a seat weapon, a passenger, a static weapon (put there by the port's SetSeat);
  - a dead body (12,000): the dying animation; the ragdoll stepping, blended between its steps, or at rest (its sounds
    ended); carried.
- **Judged by their rates:**

  | Update | Threshold | Agreement | Why |
  |---|---|---|---|
  | Update, Refresh | 99.9 % | 99.94..99.99 % | the head's look and the holds (parts 16 and 17) |
  | The seats | 95 % | 96.5..96.7 % | the 16-pass chains (part 17) |
  | The dying animation, the blend, the rest | 99.9 % | 100 % | |
  | The ragdoll's step | 96 % at a 1 % tolerance | 97.6 % | chaotic (below) |

  - One ulp of the pelvis moves the port's own 100 ms step past the usual 2e-4 in two thirds of the steps, and past 1 % in
    4.5 % (`CORE_SENS=2 CORE_TOL=1e-2`). The sides agree more often than the port agrees with itself, so the step is
    judged at 1 %.
  - Part 7's steps agree at 2e-4: they are 10..60 ms each, nearer the world's origin.
  - The step starts from a fall: the ragdoll let go standing and stepped by the port 0..12 times. Part 18's random states
    (bent, half settled) are worse conditioned (`CORE_SENS=1`: half moved), so they are used for the other paths only.
- **Test states the game does not make (each crashed one side or both):**
  - a reload without a model for a dead body (both: the model's Update);
  - a zero blend length with the raise / lower timers (a division by zero);
  - a weapon held without a right grip (SetMode clears the mode then);
  - a rifle-kind weapon without a left grip (the original's Refresh died);
  - the hand 2 / 3 (HoldProne's direction undefined);
  - the prone fit on part 16's bent skeletons (an acos past 1 in the original: its CRT's errno crashes, since that CRT
    never starts here);
  - an unconscious body's dying animation ending (its ragdoll built again: the bone lists freed with operator delete,
    which the harness does not keep).
- **Undefined in the original, not compared:** the w of the smoothed and sampled velocities and of the joints' limits
  (Attach copies them from a temp).
- **Port fixes:** none.
- **Results:** lines: human_weapon_pose.cpp 93.0 % of the 371 lines in the tested functions (345 of 886 in the file).
  Not run:
  - the constructor and destructor: the original's allocate through its own CRT heap, never started here;
  - SaveGame / LoadGame: part 19b;
  - Attach with a model, and the dying body's reload: the ragdoll built again, in each side's own blocks;
  - the ragdoll's frees in Attach;
  - the inline setters SetSeatStance, SetStanceChange and SetBlendLength (the original has no function of their own);
  - the debug joints;
  - the contacts let go (part 5's);
  - the prone fit under a hold in one update (each was tested alone).
- **Mutations** (`human/mutations/mutate_core1.py`): 345 mutants; 287 killed, 12 not building, 46 survived:
  - Attach's owner and movement (the side hook sets them);
  - the ragdoll's frees, Attach with a model;
  - one out of bounds write past the prone arrays;
  - equivalent ones, the debug switch, the contacts, float boundaries (classified in its docstring).

  A quick pass left 87; full strength killed none. Scrambling the values Attach and BindSkeleton write, BindSkeleton's
  mode 2, a weapon to let go of and the timers running out exactly killed 41.
- **Harness lessons (my mistakes):**
  - the seat test inherited the living test's last owner state. In the first run that owner was dead, so every seat
    update returned at once and "agreed" 8,503 of 8,503; the next run (an RNG shift) showed 96.5 %. Every test now sets
    the owner's health and mode and the movement's flags itself;
  - a comment in the BindSkeleton test said chain frames were set before the call, and none were: BindSkeleton's
    conversion of the chain frames was not tested until the mutants showed it (mode 2 was not tested at all);
  - the step's 2e-4 comparison failed a third of the calls at first; a no-nudge rerun (`CORE_SENS=0`, deterministic) and
    the nudged one showed conditioning, not a port difference;
  - a printf's `\n` through a bash heredoc again (the build caught it), two python edits that missed CRLF files, and a
    sed on human_weapon_pose.cpp for a hand-tested mutant that turned its line endings to LF (put back from a copy);
  - the first quick mutation pass ran against a failing baseline (the step's rate on the quick sample), so all of its
    "killed" meant nothing; it was run again after the threshold was set.

## Human part 19b (the weapon pose's saved games, `human_weapon_pose.cpp`, 2026-11-13)

- **The build:** `human/build_save.bat` (HUMAN_SAVE): build_core.bat's harness, `human/save_test.inc`'s tests alone.
  - Both sides write and read through the port's C_chunk, as part 7's ragdoll saves do; the original's operator new /
    delete already go to the harness's heap (part 4c).
  - The actors module's GetActorById answers on both sides: the human 5, the vehicles 10..13, each side its own
    vehicle objects. The net ids are set at the original's C_Actor +0x10; the port's are set through SetNetId, since
    its C_Actor keeps m_netId at offset 4.
  - The stains' SaveGame, LoadGame and SkipSaved are logged.
- **The tests (18,150 checks):**
  - SaveGame (4,500, the files compared byte for byte): living bodies (part 19's), seated ones (chain frames and joints
    of any bone, the skeleton saved as numbers), dead ones (the dying animation with its bones, the ragdoll, slide
    points, carried); stains, a vehicle, pushes and holds on points or none; animation names saved;
  - LoadGame (2,250): each file without animation names, read into another state given other values first (pointers
    it sets, gunner rotations, the ragdoll's roots); the slide points each side allocates compared apart; another
    chunk where the pose's should be (150).
- **Not compared by design:** an animation found by name on a load. The original looks it up in the animation cache
  inline (its own containers, the driver on a miss), which no redirect reaches. Names are compared when saved.
- **Port fixes:** none.
- **Results:** lines: 96.9 % of the 419 lines of SaveGame, LoadGame and the file helpers; with part 19,
  human_weapon_pose.cpp 84.8 % of 886. Not run: the lookup by name
  and the dying body's saved bones on a load, and the contacts counted after a load (found by name: part 7's).
- **Mutations** (`human/mutations/mutate_save1.py`): 461 mutants; 431 killed, 10 not building, 20 survived: the
  lookup by name and the dying body's bones (15), the contacts (1), equivalent ones (3), an Ascend after another chunk
  with nothing read after it (1) (classified in its docstring).
  - A quick pass left 33.
  - Push and hold points, a contact count, chain frames and joints of any bone, and gunner rotations and ragdoll roots
    other than the loaded ones killed 13.
- **Harness lessons (my mistakes):**
  - the first runs failed every file with a vehicle: I set the port's net ids at +0x10, the original's offset, but
    the port's C_Actor keeps it at offset 4;
  - a run took 121 s, past the mutation runner's 120 s limit: halved;
  - another python edit that missed a CRLF file.

## Human part 20a (the human's skills, names and profile, `human.cpp`, 2026-11-13)

- **The build:** `human/build_human.bat` (HUMAN_SELF): build_base.bat's harness with game/human.cpp linked,
  `human/self_test.inc`'s tests alone.
  - human.cpp emits C_Human's vtable, so every virtual and every module it calls must link. About 100 port-side fakes
    stand in for them, each logging its call or giving a scripted answer: the sight, sound, items, network and
    interface modules, the team, the events, the AI memory, the body, and the actor's base members. The module
    globals are raw storage under their names.
  - The original's calls go to the same loggers:
    - Print: empty in the release build, so both sides log the formatted line;
    - GetShortName / GetFullName, GetMoveMode / GetStateFlags, GetDifficulty;
    - the soldier store's GetSoldierProfile: a method (ECX 0x008aea1c), not the port's free function;
    - the texts' Get: its C_str is built by the original's own assignment, since the port's C_str has its layout;
    - the graphic effects' SwitchFaceTexture: each side its own object, as g++'s slot is not 0xAC.
  - Two soldier profiles, each side its own (the original's std::vector is 16 bytes, g++'s 12, so its stats are at
    +0x48); the human's profile pointer is swapped per side by the side hook.
- **The tests (48,000 checks, all exact):**
  - the gains (AddEndurance, AddMorale, AddShooting, AddStealth, AddFirstAid, AddLockpick, AddUnknownProgress,
    AddExperience): local, client and server roles, the caps, the debug lines;
  - TrainSkills: the move modes and state flags, the timers reaching their thresholds exactly or one past;
  - ApplyDifficulty: difficulties 0..5;
  - GetName: every role, a text id or none (the name's text compared);
  - SetProfile (an index or none, a profile or none), SetTexture, ApplyProfileFace (the face texture's text compared).
- **Port fixes:** none.
- **Results:** lines: 100 % of the 102 lines of these functions in human.cpp.
- **Mutations** (`human/mutations/mutate_self1.py`): 102 mutants; 94 killed, 1 not building, 7 survived: the gains'
  caps tested with > against >= (equivalent: equal to the cap, either assigns it). The first pass left 10; the timers
  set to reach their thresholds exactly killed 3.
- **Harness lessons (my mistakes):**
  - the test called GetName through the harness's logging vtable (it is virtual): C_Human::GetName is called directly
    now;
  - my redirect of GetSoldierProfile was cdecl; the original's is a thiscall method, and the unbalanced stack crashed
    the test;
  - one fake effects object for both sides used the original's slot 0xAC on the port's side too (g++ adds a second
    destructor slot);
  - an Edit that did not apply (the file had changed since I read it) went unnoticed, so one mutation rerun tested the
    old file.

## Human part 20b (the human's updates, `human.cpp`, 2026-11-13)

- **The build:** part 20a's (`human/build_human.bat`, HUMAN_SELF). Added:
  - the controller: each side its own object, as the original reads its look-at inline at +0x30. The side hook swaps
    the human's pointer. Its virtuals Update, Tick, GetFacingDir, GetViewDir and OnBodyFlags are logged (the port's
    slots by its member pointers, the original's by offset);
  - logged on both sides: the human's members in other files (OnHandsChanged, UpdateBleeding, UpdateVelocity,
    TickBleeding, UpdateSeenHumans, ForgetOldSounds, UpdateDisguise), the body's (SetFacingDir, SetLookAt, SetViewDir,
    Update, Tick, TakeFlags with a scripted answer), the events' and the formation's Tick, the player team's IsMember
    (scripted), the application's PostCommand;
  - the original's IsActive (the human's slot 0x64);
  - a mine's trigger volume, which the human stands on sometimes (its OnSteppedOn logged).
- **The tests (24,000, all exact):** Update and Tick with the action and chat timers, local, client and server roles,
  a deathmatch's automatic respawn (dead long enough, allowed, the local player's human or not), active or not, alive
  or dead, the player's team or not, a formation or none, moving or still, treading on a mine or not; RequestRespawn
  (the delay reached exactly, the movement blocking it).
- **Port fixes:** none.
- **Results:** lines: 100 % of the 62 lines of RequestRespawn, Update and Tick; human.cpp 20.0 % of 819 so far.
- **Mutations** (`human/mutations/mutate_self2.py`): 66 mutants; 61 killed, 1 not building, 4 survived: 3 equivalent,
  a float boundary. The first pass left 9; the mine killed 5.
- **Harness lessons (my mistakes):**
  - the respawn differed in 1.8 % of the updates at first: I wrote the original's local player slot directly, and
    ToOriginal overwrote it from g_script.localPlayer. Both sides now take it from that flag;
  - another `\n` through a bash heredoc in a debug printf (the build caught it), and the same in this note's first
    commit.

## Human part 20c (the human's mission start and stop, `human.cpp`, 2026-11-13)

- **The build:** part 20a's. Added:
  - logged on both sides: Activate / Deactivate, the interface's ShowTimer, the body's Create and Destroy, the
    profile's IsCooperativeGame, CreateController and AssignPlayerController, the speech sets' Acquire and Release,
    the sight module (observers and targets), the sound module's listeners (the original's slots 0x40 / 0x44), the
    periodic links, ApplyRemoteHit, the camera effects' Remove, ClearContainers, DisbandFormation, SetLeader, the
    events' Reset, the scene's GetSector;
  - the controller's Detach and deleting destructor (g++'s two destructor slots, the original's 0x18 with its flag);
  - the player team: one vtable for both sides (raw storage; the original's GetPlayerTeam answers the same object):
    AddMember at the original's 0x08, RemoveMember(C_Human *) at its 0x24, g++'s slots beside them;
  - a vehicle whose LeaveSeat is logged (the original's slot 0x80);
  - the model's "z" and "f" dummies, and a parent that is not a sector;
  - the original's speech sets marked as made (0x008ac448): its first use builds them inline.
  - Two calling conventions I had wrong at first: PeriodicLinks_Add / _Remove are stdcall in the original (RET 8 /
    RET 4), not cdecl; the unbalanced stack crashed the original's StartMission with no crash report.
- **The tests (16,000, all exact):** StartMission and StopMission, client and server, initialised or not, every role,
  a script or none, local, client and server games, co-op or not, spawn protection, the ids held or none, a speech set
  or none (or missing), the profile's death flag, a seat, the body's dummy or only the head's, the model outside a
  sector, a dead human joining a network game.
  - Each side's AI memory is compared on the bytes the original's inline reset writes. The port also clears the
    fields its comment marks as not in the original; the owner is each side's own human.
  - The face texture's text is compared.
- **Port fix (on main):** StartMission asked the profile's IsCooperativeGame before choosing the controller, for every
  human. The original asks only for the enemies and the neutrals (local, then server, then co-op: 0x0059f5f5). It is
  now asked there.
- **Undefined in the original, not compared:** the scale's w (copied from a temp).
- **Results:** lines: 97.9 % of the 141 lines of StartMission, StopMission and their helpers (not run: freeing heard
  sounds and pending orders); human.cpp 37.0 % of 816 so far.
- **Not run:** the human's containers (heard sounds, pending orders, seen humans) holding anything: std:: containers
  in the port, MSVC6 ones in the original, not mapped between the layouts.
- **Mutations** (`human/mutations/mutate_self3.py`): 148 mutants; 121 killed, 3 not building, 24 survived:
  - the containers (12);
  - the body's empty static calls (8);
  - the port's own speech sets (2);
  - a frame update the harness does not see (1);
  - a debug message (1).

  The first pass left 39; varied start fields, the dummies, the parent, a seat and a missing speech set killed 15.
- **Harness lessons (my mistakes):** the two stdcall functions I redirected as cdecl (above); and Python edit scripts
  written through bash heredocs collapsed `\n` again, so the scripts are files now.

## Human part 20d (the human's restarts, `human.cpp`, 2026-11-13)

- **The build:** part 20a's. Added:
  - the fakes that make a controller (CreateController, AssignPlayerController) install each side's controller
    object, of a scripted type; its SetSpectating is logged (the original's slot 0x60);
  - logged on both sides: Reactivate, ShowFirstPersonView, the first person view's Refresh, the surface marks'
    SetHumanVisible (fastcall in the original: the human in ECX, the flag in DL), the body's Reset and
    SetModelVisible, FindSoldierState (answers none), PutOnUniform, the graphic effects' FadeHuman (slot 0x74);
  - the uniform's container with GetItem (slot 0x40, one vtable for both sides) and an item whose uid is at +0x10.
- **The tests (16,000, all exact):** RestartSinglePlayer (client and server), RestartCoop, RestartMultiplayer,
  RestartMission:
  - every role and controller type, faded out or not, a script or none;
  - local, client and server games, co-op or not;
  - a uniform or none;
  - a net id at the end of the player slots;
  - the model switched off.

  The face texture's text and the model's on flag are compared.
- **Port fixes:** none.
- **Results:** lines: 97.9 % of the 189 lines of the restarts and SetBodyDetached; human.cpp 59.7 % of 816 so far.
  Not run:
  - Setup from a saved soldier's state (part 20e tests Setup itself);
  - SetBodyDetached bringing a detached body back: its movement's OnFrozen needs the hit spheres only the movement
    harness sets up (part 5 tests it there).
- **Mutations** (`human/mutations/mutate_self4.py`): 34 mutants (the restarts repeat most lines); 31 killed, 3
  survived: Setup from the saved state (2), an equivalent condition (1). The first pass left 6.
- **Harness lessons (my mistakes):** I defined the port's controller object twice (once with the fakes, once in the
  test's namespace): the fakes installed one, the side hook compared the other. And the surface marks' call is
  fastcall, which I first redirected as cdecl.

## Human part 20e (the soldier's setup and equipment, the multiplayer spawn, `human.cpp`, 2026-11-13)

- **The build:** part 20a's. Added:
  - logged on both sides: the items module's CreateItem (an item or none: scripted; the items' uids at +0x10),
    ClearActorContainers (a method of the items module in the original, 0x0098b020: I redirected it as cdecl first,
    and the unbalanced stack crashed the test), the network game's SetPlayerTeam;
  - the soldier's state: the original gets its own copy (MSVC6 vectors are 16 bytes, g++'s 12). The bytes up to the
    belt are the same; the belt and backpack lists are MSVC6 vectors over copies of the entries; the load is at
    +0x8C. The entries' uids the network game's items fill in are compared after each call;
  - the network replication: the human's net object, its place property, the position track (the original's at the
    property's +0x30; each side its own track, as g++'s slots of SetValue / SetNetValue / SetRate are not the
    original's 0x1C / 0x20 / 0x24);
  - the team's SelectMember, SetWatchedHuman and UpdateCameraTarget, the interface's SetScreen and ShowSquadMember,
    the camera module's SetSpecialMode (the original's slot 0x44);
  - the human's own StartMission / StopMission (slots 0x3C / 0x40), logged: tested in part 20c.
- **The tests (20,000, all exact):**
  - CreateEquipment (a state or none, items or none);
  - Setup (every role, stats or the profile's, a speech set or a random one, local and network games, a VIP: its slot
    on the game compared);
  - Respawn (a state or none, stats or none, the local player's or another's; the model's local matrix compared).
- **Port fixes:** none.
- **Results:** lines: 100 % of the 94 lines of CreateEquipment, Setup and Respawn; human.cpp 71.3 % of 816 so far.
- **Mutations** (`human/mutations/mutate_self5.py`, `mutate_self6.py`): 72 mutants; 64 killed, 5 not building, 3
  survived: an equivalent release, the body never detached, and the fake frames' SetDir ignoring the roll.
- **Harness lessons (my mistakes):**
  - ClearActorContainers redirected as cdecl (above);
  - the restarts' test changed the human's net id after the local player's slot was taken from it: the port compared
    a stale slot (ToOriginal sets the original's afresh). Found by Respawn's UpdateCameraTarget differing in 6.6 % of
    the calls.

## Human part 20f (the human's saved games, mission properties, SetFrame and CopyFrom, `human.cpp`, 2026-11-13)

- **The build:** part 20a's. Added:
  - logged on both sides: the actor base's SaveGame, LoadGame, SetFrame (a scripted answer) and CopyFrom, the body's,
    the events', the controller's (slots 0x2C / 0x30), the formation's and the AI memory's saves and loads (the
    formation's save writes a word: its chunk is then not empty), GetActorById, IsUnconscious, the enemy records' Add,
    the sight module's SetObserverState, the vehicle's SetSeatOccupant (slot 0x74);
  - the original's _aligned_malloc / _aligned_free (part 7's redirects): a load without a formation makes one;
  - the original's formation constructor made empty, as the port's fake: the original's builds its slots with code
    this test does not set up (it crashed the test with no report).
- **The tests (all exact):**
  - SaveState and SaveGame inside an outer chunk, the files compared byte for byte;
  - LoadState and LoadGame from inside it (the port's files), into other states: a seat (id 0 or not), a carried
    actor, a face, dead or unconscious, a formation and an AI memory or none (made by the load: their pointers are
    each side's own; whether there is one is compared);
  - WriteProperties (the files compared), ReadProperties (versions 9..15, short chunks, a text id of 0);
  - SetFrame (a model or another frame, the actor's answer), CopyFrom (a source human in each layout).
- **Port fixes:** none.
- **Results:** lines: 100 % of the 205 lines of these functions; human.cpp 96.4 % of 816. Not run in human.cpp:
  - the creator, the constructor and the destructor;
  - freeing heard sounds and pending orders (the containers are not mapped between the layouts);
  - Setup from a saved soldier's state in RestartSinglePlayer;
  - SetBodyDetached bringing a body back.
- **Mutations** (`human/mutations/mutate_self7.py`): 139 mutants; 132 killed, 2 not building, 5 survived (equivalent).
- **Harness lessons (my mistakes):**
  - **the first mutation run's 137 "killed" were timeouts.** The whole test had grown to 115 s; mutrun runs six at
    once and counts a timeout as killed (the baseline runs alone and passed). Mutants on lines the test never ran were
    "killed". Coverage showed the loads' loops never ran: on a freshly opened file the chunk is "at end" before any
    chunk is entered, while the game calls the loads inside the human's chunk. The tests now use an outer chunk;
    SELF_ONLY runs one part's tests, and the file tests run a quarter as often. Parts 20a..20e's lists were rerun with
    their parts alone: the same results as before (those runs had taken a few seconds per mutant);
  - the save-and-load test changed the actors' ids between a save and its load (the load then found no vehicle and
    crashed);
  - self_test.inc had mixed line endings (CR CR LF in 315 lines) from an edit script that turned LF into CRLF in text
    that already had CR: normalised to LF.

## Human part 21a (the human's sides, teams, disguise and stats, `human_combat.cpp`, 2026-11-14)

- **The build:** `human/build_combat.bat` (HUMAN_COMBAT): build_human.bat's harness with game/human_combat.cpp linked
  too, `human/combat_test.inc`'s tests alone.
  - Part 20's fakes of the combat functions are left out (`#ifndef HUMAN_COMBAT`), and the original's GetStat,
    IsLocalPlayer, IsServerSimulated and UpdateDisguise run real again.
  - About 40 more port fakes (the shot manager, the network game's teams, the vehicle's controls, the body's hit and
    death calls, the scripts' OnDeath, ...).
  - The player team's IsMember answers by object. A second human ("other") is copied in both layouts through
    ToOriginal, as part 20f's CopyFrom.
  - The worn items' containers (one each, GetItem: slot 0x40 on both sides), items and their definitions: the
    original's item +0x04 -> definition +0x44, the port's at its own offsets.
  - The game's uniform-disguise flag (the original's game +0x34). A MeleeTracker (its type at +0x18 on both sides).
- **The tests (36,000, all exact):** IsFriendOfInNetGame, IsFriendOf (another human or a MeleeTracker in local games),
  IsDisguisedEnemy, GetSide, GetTeam, SetPlayerTeam, IsLocalPlayer, IsServerSimulated, UpdateDisguise, SetDisguise,
  GetDisguisePercent, NotifyShotAt, GetStat, GetStatFactor, SetSkills, ChangeMorale, Get / SetHealthPercent,
  GetStaminaPercent:
  - every role, local, client and server games, co-op or not, deathmatch or team games, the uniforms disguising or
    not;
  - members of the player's team or not, allied, axis or neutral items worn or none, carrying a body;
  - stats of 0, negative and large.
- **Port fixes (on main):** GetStaminaPercent and GetHealthPercent converted an infinity (an endurance or an initial
  health of 0) to INT_MIN; the original's __ftol truncates through 64 bits, whose "indefinite" value has a low word of
  0, so it returns 0. Both truncate through int64_t now.
- **Not observable, not compared:** the worn containers' GetItem (a getter): the original's GetSide asks for the
  uniform twice and its UpdateDisguise asks the containers in another order, with the same results.
- **Results:** lines: 100 % of the 122 lines of these functions; human_combat.cpp 19.6 % of 622 so far.
- **Mutations** (`human/mutations/mutate_combat1.py`): 90 mutants; 74 killed, 5 not building, 11 survived (clamps,
  equivalent at their boundaries).
- **Harness lessons (my mistakes):**
  - the test passed a MeleeTracker to IsFriendOf in network games, which treat every actor as a human (both sides
    crash on it); only local games pass one now;
  - a rerun of the survivors ran against a failing baseline (GetHealthPercent's infinity, found by the new random
    values) and reported every mutant killed: the baseline line must be read first.

## Human part 21b (the human's bleeding, motion, visibility, aim points, box and fading, `human_combat.cpp`, 2026-11-14)

- **The build:** part 21a's. Added:
  - the original's TickBleeding, UpdateBleeding, UpdateVelocity and ApplyRemoteHit run real again (part 20 logged
    them);
  - logged or scripted on both sides: the body's GetVisibilityFactor and GetGround, the sight module's
    GetVisibleParts, the shot manager's SprayBloodRandom, the graphic effects' GetGroundCover (slot 0x94), the coop
    team (the original's GetCoopTeam answers the port's);
  - the model's body visual "a": its joints (+0x214: 25 dummies) and its own box (random, each corner apart);
  - a model for the shooter at 0..25 m.
  - COMBAT_ONLY runs one part's tests (21a, 21b ...).
- **The tests (24,000, all exact; the box's corners within 1e-5):**
  - TickBleeding, UpdateBleeding (the drops' timer at its edges, moving or not), UpdateVelocity (a seat or none);
  - GetVisibility: every stance and pace, the quiet time at the alert windows, the coop team on a server, the body's
    factor, the ground's cover, no controller;
  - GetRandomBonePos, GetAimPoint (the parts seen, the shooter a player or not, near or far, no shooter);
  - GetBoxCorners, FadeOut, FadeIn.
- **Kept out:** GetAimPoint with only other part bits seen: the original divides by zero there (the port's fix returns
  a random bone instead: its one line not run).
- **Port fixes:** none.
- **Results:** lines: 99.4 % of these functions' 156; human_combat.cpp 44.5 % of 622 so far.
- **Mutations** (`human/mutations/mutate_combat2.py`): 151 mutants; 107 killed, 9 not building, 35 survived:
  - GetVisibility's minimum clamps, which can never act (dead in the original too: 26);
  - exact float boundaries (3);
  - equivalent ones (4);
  - out-of-bounds writes of a ninth corner (2).
- **Harness lessons (my mistakes):**
  - the first visual "a" I made was never found: the base harness's model has one already (FindChildFrame found it
    first, without joints);
  - that visual's box was degenerate, so every GetBoxCorners mutant survived the first run;
  - the other human shared the tested one's model, so the aim points' distances were all 0;
  - another edit script through a heredoc collapsed its backslashes, and one converted line ends twice (CR CR LF):
    fixed with Edit and normalised.

## Human part 21c (the human's messages and deaths, `human_combat.cpp`, 2026-11-14)

- **The build:** part 21b's. Added:
  - logged or scripted on both sides: the actors' GetHuman by id (the tested human and the other), the local team,
    the game's PrintMessage (cdecl, varargs: the text formatted, then logged), the quick messages' texts, the player
    names, the body's PlayDeath and ApplyHit, the periodic links' SetRate, the teams' and the coop team's
    OnMemberDied, OnLeaderDied, the car's throttle, steering, brake and handbrake, StopFrameSpeech, IsSurrendered,
    HasSeenEnemy, OnSightEvent, the net game's OnPlayerKilled, the damage flash, OnDamaged, the hit sounds, effects
    and blood, the music (the sound module's slot 0x68);
  - the controller's OnDeath, OnKilled, OnKnockedOut and OnKilledEnemy slots on both sides;
  - the original's GetName and GetMaterialId in its human vtable (slots 0x04, 0xD8), the port's through thunks;
  - the local player's slot written for the original by a hook chained after ToOriginal's (the tested human, the
    other one or nobody);
  - the texts show their arguments: each multiplayer message's text ends in as many "%s" as it takes, so a swapped
    or missing argument shows in the printed line;
  - each human its own name and face strings (kept reps): a copied human shared the C_str reps without a reference.
- **The tests (24,000, all exact):** ShowHitMessage, PrintText and PrintNamedText (texts with two arguments, one or
  none), SayQuickMessage, SetChatText (empty, none, up to 80 characters), OnDeath, ShowDeath, OnKilledHuman, KnockOut,
  ApplyRemoteHit:
  - every hit type and zone (and zones past the table), the killer the human itself, the other or an unknown id;
  - local, client and server games, team games or deathmatch, the local team;
  - every role and controller type, the quiet time at 60 s, the victim surrendered, unconscious or seen;
  - OnKilledHuman's seen lists (enemies, friends, traitors) with the victim in none, one or several places; the
    original's as MSVC6 vectors (an allocator word, then first, last, end);
  - a car or a static weapon as the vehicle, a formation or none, a remote kill's health at -1..1.
- **Port fixes:** ShowHitMessage took both names before comparing the teams; the original compares them first (the
  calls' order differed in the log).
- **Results:** lines: 100 % of these functions' 207; human_combat.cpp 81.5 % of 623 so far.
- **Mutations** (`human/mutations/mutate_combat3.py`): 172 mutants; 153 killed, 13 not building, 6 survived, all
  equivalent (buffer sizes, the named line PrintNamedText builds and never prints, a terminator written over, -2 for
  no target).
- **Harness lessons (my mistakes):**
  - the original crashed silently in one ShowHitMessage: the victim's random name text id was one of the multiplayer
    messages, whose test texts end in "%s", and the formatted line is formatted again by PrintMessage. Names are kept
    out of that range now (self_test.inc's RandomSide; part 20's tests are unchanged otherwise). In the game a name
    with "%s" would do the same in both;
  - the first text suffix gave every text two "%s", more than some texts' arguments: undefined in both; now per text;
  - the other human's name was empty in the original's copy, and both copies shared string reps;
  - the seen lists were empty on both sides, so OnKilledHuman's search never ran: 13 survivors until they were
    filled;
  - two more edit scripts through heredocs collapsed their backslashes; fixed with files written by Write.

## Human part 21d (the human's hits and collisions, `human_combat.cpp`, 2026-11-14)

- **The build:** part 21c's. Added:
  - the original's calls of Hit logged as the port's are: GetCheatValue (the index in ECX), IsHumanSpeaking
    (fastcall), the controller's Say, the body's Die, Cripple, Ragdoll and ResetMovement, the human's GetViewDir, the
    frame script's OnDeath;
  - the controller's OnHit (slot 0xBC) and OnCollisionEx (slot 0xC8) on both sides;
  - Hit itself real in both vtables (the original's slot 0xD4; the port's slot calls the member): a reflected team
    hit and a run-over call it again;
  - a car for each side (its driver and crew are that side's humans), its seats through its own vtable; the port's
    vehicle speed written at the offset GetSpeed reads (found by probing: the port's C_Vehicle is not laid out as the
    original's);
  - the original's integrity check made to pass (its two table entries equal), as the port assumes;
  - the frame's data: none, without a script, with one.
- **The tests (24,000, all exact):**
  - Hit (16,000): every hit type (bullets more often) and zone, past the table too; amounts whole, fractional, 0 and
    -1; the attacker none, the human itself or the other; local, client and server games, co-op or not, team games or
    deathmatch; the sniper cheat, every difficulty, no team damage with the human in the player's team or not; friendly
    fire with its share, the spawn protection at its edge; the leg hits 0..3, a dead human, a zero direction;
  - OnCollisionEx (8,000): a car or a static weapon at 2 m/s exactly, faster, slower or NaN; a driver or none; 0..4
    seats with the human, the other or nobody in each; no controller; the controller's answer.
  - After each, the other human's health, leg hits and disguise are compared too (a hit reflected onto it, its
    disguise as a crew member).
- **Kept out:** the original's failing integrity check (its own shots hurt the player 1 % of the time): the port
  has no check.
- **Port fixes:** none.
- **Results:** lines: 100 % of these functions' 113; human_combat.cpp 99.8 % of 623, done: the one line not run is
  GetAimPoint's fix for a case where the original divides by zero (part 21b). Hit type 15's flag (HitTypeFlags) was
  added to the tests after the mutation run, which covered Hit and OnCollisionEx only.
- **Mutations** (`human/mutations/mutate_combat4.py`): 124 mutants; 113 killed, 7 not building, 4 survived, all
  equivalent (the integrity check's branch, the helmet's draw at exactly 0.3).
- **Harness lessons (my mistakes):** I wrote the port car's speed at the original's offset; the port's vehicle is laid
  out differently, so GetSpeed read 0 every time. A check against GetSpeed showed it, and the offset is now found by
  probing.

## Human part 22a (the human's controller made and its wrappers, `human_control.cpp`, 2026-11-14)

- **The build:** a new one, `human/build_control.bat` (HUMAN_CONTROL; suite `human.control`): part 21's harness with
  `human_control.cpp` linked (part 20's and 21's fakes of its functions left out). Added:
  - the controllers CreateController makes: the port's derived vtables in raw storage under their symbols' names (their
    code is not linked), the constructors logging the type; the original's inline constructions run with its
    constructors (0x582ff0, 0x56ee90), the squad's Create and the aligned allocation redirected, and its six real
    vtables' Attach, Detach and deleting destructor slots logged;
  - each side's controller object for the wrappers, its fields in its own layout (the port's by member, the player's
    and the scripted controller's by the type), its virtuals and the squad controller's logged (slots by member pointer
    and by offset);
  - the player's, AI's and scripted controllers' members the human calls, the body's, the events', the movement's, the
    car's, the network game's player names, the squad commander, the hands weapon with its definition and component:
    logged on both sides, their answers scripted;
  - the human's OnMessage / Unknown20 real in both vtables (HandleMessage calls them through it).
- **The tests (69,953 calls, all exact; 76,000 iterations, those without a controller skipped where the original
  reads it):** CreateController, AssignPlayerController, SetScripted, StartScriptedController
  (every type the original knows, the same type again, no controller); the wrappers: the controller's type, commands,
  directions, accuracy (no weapon, equipment, a weapon), the move, state, AI and stance flags, the speed class, MakeNoise
  at the 1 s quiet time, unconscious, in play, DetachFromWorld, spectating, remote, the move speed, water, surrendered,
  suspended, the messages (the tested human, the other, none; player, remote or other controllers), the sight tests
  (the local player or not), the path height, the light, the vehicle's leaving, the remote requests, the weapon stance,
  the AI's path points, the player controller's input, equipment, commands, view mode, damage and breath, the squad's
  orders and icons, LookAround, RaiseWeapon, LowerWeapon; no controller where the original tests for one.
- **Kept out:** CreateController's unknown types (0, 2, 9): the original attaches a null controller and crashes; the
  port checks.
- **Port fixes:** none.
- **Results:** lines: 261 of these functions' 264 (the three not run: the unknown type's case).
- **Mutations** (`human/mutations/mutate_control1.py`): 127 mutants; 122 killed, 4 not building, 1 survived
  (equivalent: the unknown type's case). Added in part 22b: 6 mutants of the repeated condition lines, all killed.
- **Harness lessons (my mistakes):** the new controllers were named after their constructors logged (the names differed
  in the first run); the original's deleting destructor's flag was logged where the port has none; the other human was
  never made, so HandleMessage reached a zeroed object.

## Human part 22b (the human's body, names, formations and the scripts' commands, `human_control.cpp`, 2026-11-14)

- **The build:** part 22a's. Added:
  - the network game's players for GetProfileUnknown24: the port's LockPlayers answers a table (part 20's fake answered
    none), its player vector found by probing GetPlayerCount; the original's LockPlayers answers a vector in its
    layout; UnlockPlayers counted on both sides;
  - the formations: each side its own objects (the port's made with its constructor, freed by its delete; the
    original's through its aligned allocation), the members in the slots each side's humans, ToOriginal mapping the
    port's to the original's; their AddMember, RemoveMember, SetLeader, SetType, SetScale kept as the original's
    layout keeps them;
  - the profile's names in both layouts (each side's own strings), the other human and the car mapped by ToOriginal.
- **The tests (80,000, all exact):**
  - the body's command, carry mode, animation, foot; GetEnvironmentFlags (no vehicle with the ground or none, a car,
    a static weapon), SetUnknown94 (the same value or not);
  - the names: last, first, short, full (no profile: the frame's or the human's own; a custom name or none, empty
    names), the player name, the multiplayer rank (local, or the player's slot in a table of 1..4);
  - SetLeader, DisbandFormation, GetFormationType: nobody leading, the other leading the tested human or the tested
    one leading the other, the leader and its follower in any two slots, the member count sometimes more than the
    slots hold, a formation without slots; the new leader none, the other, the human itself; compared by a summary of
    both humans' leaders and formations (members by slot, count, type, leader);
  - the scripts' 41 commands with the scripted controller or another, SetSniper.
- **Port fixes:** none.
- **Results:** lines: 100 % of these functions' 252; human_control.cpp 99.4 % of 516, done: the three lines not run are
  CreateController's unknown type (part 22a).
- **Mutations** (`human/mutations/mutate_control2.py`): 123 mutants (77 generated, 46 written for the condition lines
  found more than once in the file, which the generator skips); 116 killed, 6 not building, 1 survived (equivalent:
  SeesFriend's type test, false either way).
- **Harness lessons (my mistakes):**
  - the formation constructor's fake returned nothing; the original's SetLeader uses the `this` it returns: a stack
    of crashes until found;
  - my first formation states were ones the game never makes (humans leading themselves, followers whose leader has no
    formation): nested disbands freed formations still in use; the formation's pointers were not mapped by ToOriginal
    either, so the original ran on the port's objects;
  - my own summary and AddMember fakes read a formation without slots;
  - debug prints written through a bash heredoc lost their backslashes; the build failed and two runs used the old
    executable before I noticed;
  - UnlockPlayers was not checked at all until a mutant survived.

## Human part 23a (the controller's life cycle, small virtuals, move flags, inventory requests and messages, `human_controller.cpp`, 2026-11-14)

- **The build:** a new one, `human/build_controller.bat` (HUMAN_CONTROLLER; suite `human.controller`): part 21's
  harness with `human_controller.cpp` linked. Added:
  - each side's controller: the port's class with its real vtable, the original's 0xC0 bytes made by its own
    constructor with its real vtable; before each call the port's fields copied into the original's layout (the
    table generated by `_re/tools/layout_offsets.py`; pointers mapped: the humans, the controllers), after it every
    field compared, and the messages each queued (by class through each side's vtable, field by field in each side's
    layout; the port's message vtables in raw storage under their symbols' names);
  - the controller's callees logged on both sides with scripted answers: the walking network, the human's item
    functions, the game's alarm, the network object's removal, the first-person view's inventory event, the body's
    command, climb and work state, the aligned allocation and free (the messages' type logged); the worn items'
    definitions a vtable (GetBehaviour);
  - a target frame for IsFacing.
- **The tests (60,000, all exact):** the constructor (every type; the fields the original writes), the destructor
  (empty queues), Attach (with a previous controller or none, enemies and snipers raising the alarm, a local or network
  game, a walking network or none), Detach, OnDeath (a vehicle seat or none, the hands item thrown or dropped, a local
  or network game, an interaction going on), OnKilled, OnKnockedOut, SetUnconscious, AdjustAim, RequestAction, the
  directions, AdjustToPathHeight, Unknown5C, SetSpectating, IsSpectating, OnCollisionEx, SetAIMode, SetMoveFlags (each
  group, several, the medium group whole, none), UnknownE8, GetAimBonus, IsSurrendered, Unknown104, UpdateHandsItem,
  NotifyHud, IsFacing, OnUniformChanged, the inventory requests (moved or not, local or network), OnItemDropped,
  SendRemoteRequest (server or client), SendMessage23, ClearMessagesToServer / ToClients with messages queued.
- **Kept out:** IssueCommand and RequestLeaveVehicle (they run ExecuteCommand: part 23c).
- **Port fixes:** the base controller's SetSpectating did nothing and IsSpectating returned false; the original's
  (0x524d30, 0x524d40) keep the flag at +0x70, and `game_rounds.cpp` asks every human's controller. (The original's
  constructor leaves the flag as the allocation had it; the port's starts false.)
- **Not compared:** the queued transfer message's type word (the original stores the move message's, the port the
  transfer's; both send the transfer's), the dropped item direction's w (the original copies a word past the vector),
  the fields the original's constructor leaves unset.
- **Results:** lines: 229 of these functions' 238 (the 9 not run: IssueCommand and RequestLeaveVehicle, part 23c).
- **Mutations** (`human/mutations/mutate_controller1.py`): 162 mutants (132 generated, 30 written for the lines found
  more than once in the file); 139 killed, 13 not building, 10 survived (equivalent or not comparable: listed there).
- **Harness lessons (my mistakes):** the first message comparison read words past each message's end and took the
  port's layout for the original's (the drop message's vector is at +0x0C in the port's g++ build, +0x10 in the
  original); the TakeFromSupplyBox fake logged out-parameters the original leaves unset; Attach first ran on a
  controller that already had the human and its events; another heredoc collapsed backslashes (a suite line came out
  right only by chance).

## Human part 23b (the controller's controls, `human_controller.cpp`, 2026-11-14)

- **The build:** part 23a's. Added: the car's ApplyControls and the submarine's SetControls logged on both sides.
- **The tests (40,000, all exact):** ControlVehicle (a car, a submarine, another kind; the local player or not, the
  driver's seat or another), ApplyMoveControls (the axes and buttons on, off or partly, the flags and the pace asked
  for, the walk speed or none, diving gear, the stamina at the dive's 10, a vehicle seat; the climb command it issues
  runs ExecuteCommand on both sides), EncodeControlBits, DecodeControls (no velocity, a vertical one, climbing or not,
  every bit pattern; the arrays each side's own copy).
- **Not compared:** the look-at's w after ApplyMoveControls (the original leaves a stack value there, as the port's
  comment says).
- **Port fixes:** none.
- **Results:** lines: 176 of these functions' 177 (the one not run: the stamina clamp after a dive, unreachable in both).
- **Mutations** (`human/mutations/mutate_controller2.py`): 181 mutants (175 generated, 6 for repeated lines); 160
  killed, 9 not building, 12 survived (equivalent: listed there).
- **Harness lessons (my mistakes):** I called the original's EncodeControlBits as the port's (returning the bits); it
  writes them through a second argument and pops two, so the call unbalanced the stack and overwrote the test's own
  locals: the crash looked like a null vehicle until the locals were printed.

## Human part 23c (the controller's interactions and commands, `human_controller.cpp`, 2026-11-14)

- **The build:** part 23b's. Added:
  - the actors the commands find by id (100 + the kind: a door, a mine, an explosive place, a car, a submarine, a
    static weapon, a Ju 88, a radio, a switch, a photo place, a document place, a wire cut, and the other human):
    each side its own object, the type, id, frame and temporary flag at its offsets, one vtable each side (the seats'
    blocking logged), mapped by ToOriginal; the door's key, the explosive place's state and the photo place's flag at
    each side's offsets (the port's found through the inline getters);
  - GetActorById and the temporary list's FindActor answering them on both sides; the temporary actors' vector in
    both layouts (the original's at 0x0089b5f0);
  - their members logged on both sides (doors, mines, explosive and document places, radios, switches, wire cuts, the
    car's charges and ray), CreateMine (taking the item from the hands, as the game's does: put back by ToOriginal
    before the original runs, the container being shared), the item's GetFrame, the photo flash, the visual's
    position, FindItem, FindWireCutter, the network track's GetTarget;
  - UseMedkit logged on both sides (a test class on the port's, the original's vtable slot 0x14): part 23d tests it;
  - the original's commands in its own layout (its S_HumanCommand is polymorphic: a vtable first);
  - a body frame over the ground, turned and scaled (the mine's ray down).
- **The tests (60,000, all exact):** EndInteraction (each interaction, completed or not, a server or a client, no
  target where the original survives it), UpdateInteraction (the actor gone, the place's state changed, a human's
  health gone, the time running out at its edge), LeaveVehicle (each vehicle kind, a carried body, a network game),
  RequestLeaveVehicle, IssueCommand, UseActor, ExecuteCommand (every command type and some unknown, actors of every
  kind or none, seats blocked or not, a server or a client, lying or not, a medkit's uid); after each, the photo
  place's flag and the other human's action bar.
- **Kept out:** what the original does not survive and the port checks: a missing door locked on the server, a mine laid
  without a hands item on the server, a missing document place, an interaction's missing target where the original
  reads it.
- **Port fixes:** none.
- **Results:** lines: 100 % of these functions' 326.
- **Mutations** (`human/mutations/mutate_controller3.py`): 227 mutants (166 generated, 61 for repeated lines); 208
  killed, 14 not building, 5 survived (equivalent: listed there).
- **Harness lessons (my mistakes):** I passed the original commands in the port's layout (it read the type from the
  wrong word); I took the original's actors module and temporary list addresses from guesses and a header comment,
  both wrong; the other humans held different controllers on the two sides; my CreateMine fake emptied the shared
  hands container before the original ran, which looked like the original's ray missing; another heredoc collapsed a
  backslash (caught by the script's assert).

## Human part 23d (the controller's saved games, speech, stamina and healing, `human_controller.cpp`, 2026-11-14)

- **The build:** part 23c's. Added:
  - the speech set's Say, SayNow and SetDelay logged on both sides; the sound module's PlayDubbing,
    PlayDubbingWithNoise and GetDubbingSampleSize (the original's slots 0x50, 0x4C, 0x94); the speech code helpers on
    the port's side are the original's own (pure functions);
  - the players the server's Say looks at (the actors' first 32: the tested human, the other one, none), their
    IsSpectating and IsInPlay; the camera as the scene's active one;
  - the medkit: each side its own object (Consume and GetCharges are at other slots on each side), its charges each
    side's; FindItemByUid, FindContainerOfBehaviour, RemoveItem logged; the humans by id for the patients;
  - the human's GetHitPoints real in both vtables (UpdateStamina reads the strength).
- **The tests (26,000, all exact):** SayNow (the player's soldiers' quick messages, within the range or not, silent),
  Say (no speech set, no sample, the delay running, swimming, the AI's hearing on a server or not, the clients told
  when a player in play is in range), UpdateStamina (each speed class and stance, the load, the endurance against its
  initial value, the maximum and 0, marching ended), UseMedkit and Heal (each patient or none, the human's own medkit
  found or by uid, its charges, the first aid skill, a full health, a local, server or client game); SaveGame and
  LoadGame (an interaction or none, its target an actor, a temporary one or the human healing itself; the files
  compared byte for byte, the port's file loaded on both sides; a chunk after the controller's; an empty chunk).
- **Kept out:** SayNow without a speech set (the original calls it; the port checks); an initial endurance of 0
  (divides by zero in both); saving an interaction without a target (the original reads it; the port checks).
- **Not run:** LoadGame's state chunk shorter than its fields (the size checks' false branches).
- **Port fixes:** none.
- **Results:** lines: 100 % of these functions' 180.
- **Mutations** (`human/mutations/mutate_controller4.py`): 180 mutants (167 generated, 13 for repeated lines); 159
  killed, 12 not building, 9 survived (equivalent or not comparable: listed there).
- **Harness lessons (my mistakes):** the test first took 57 s alone (over a third of the mutation timeout): fewer
  iterations; SayNow first ran without a speech set and the endurance with an initial value of 0.

## Human part 23e (the controller's weapons, `human_controller.cpp`, 2026-11-14)

- **The build:** part 23d's. Added:
  - a gun and a grenade on each side (each side's vtable; the time, rounds, attack time, flags and throw strength at
    each side's offsets), their Fire, FireSecondary, Reload and GetAmount logged; the weapon component's attack
    delay at each side's offset; the attack time and flags compared after each call;
  - the shot dummy as the original's own lookup (a cache, else the model's "shdum" child; each side's cache): its
    UseItem inlines that lookup, so a plain fake could not give both sides the same calls;
  - the hands item each side's (the worn hands container is shared: the original's weapon put in by ToOriginal);
    the belt grenade, the first-person view's aim (the original's return the vector through a hidden pointer);
  - the vehicles' HasSeatWeapon and ApplySeatControls (the actors of part 23c; the original's slots 0x98, 0xC0);
  - RequestAction and AdjustAim logged by the test class and the original's base vtable slots (the base's do
    nothing: their arguments otherwise unseen).
- **The tests (40,000, all exact):** OnBodyFlags (every flag alone or together, the hands item's time against the
  mission time, the local player or not, a network game), UseItem and UseItemAt (fire or secondary, the local
  player's aim from the camera or the shot dummy, the grenade thrown upward, the first-person view or not),
  RaiseWeapon, LowerWeapon, ApplyWeaponControls (no controls, a vehicle's seat weapon, the driver, the belt grenade,
  each behaviour, fire held or pressed, reloading from the belt or an empty weapon, the weapon lowered, marching, the
  attack time at the mission time).
- **Kept out:** what the original does not survive or reads past an object and the port checks: firing through no
  item (UseItem, OnBodyFlags' flag 0x20000 with empty hands), the shot dummy of an item that is not a weapon.
- **Not compared:** Reload's ammo uid going in (the original's unset, the port's -1), the grenade direction's last
  bits (logged and compared within the float tolerance: x87 against SSE rounding of its normalization).
- **Port fixes:** none.
- **Results:** lines: 100 % of these functions' 130; human_controller.cpp 99.9 % of 1045, done: the one line not run
  is the stamina clamp after a dive (part 23b; unreachable in both).
- **Mutations** (`human/mutations/mutate_controller5.py`): 126 mutants (108 generated, 18 for repeated lines); 114
  killed, 10 not building, 2 survived (equivalent: the grenade's 0.7 boundaries).
- **Harness lessons (my mistakes):** my first hands item was a plain worn item with a firearm's behaviour (no vtable:
  a crash); I kept the non-weapon kinds and null items that only the port survives; the shot-dummy fake did not do
  what the original inlines; I hashed float outputs bit for bit.

## Human part 24 (the human's senses and events, `human_senses.cpp`, `human_events.cpp`, 2026-11-14)

- **The build:** `human/build_senses.bat` (HUMAN_SENSES, `senses_test.inc`): part 22's harness with both files linked.
  - The original keeps the human's seen lists and remembered sounds, and the events object's two deques and two
    vectors, in MSVC6 form: each is rebuilt before the original's call and read back after it. The deques are
    blocks of 1024 pointers through a map; the first element anywhere in its block, sometimes near its end, so
    pushes and pops cross blocks.
  - The events are shared (the same layout on both sides): their bytes are put back before the original's call and
    compared after it.
  - Each side's event pool is its own: what a call released is read back in order (the port's drained down to a
    sentinel it was given first, the original's list emptied first). The original's pool was never constructed
    (no static initialisers run): its block size of 50 is set by the test.
  - Fakes, logged on both sides: the sight module's targets (each side's objects; the original's target vector
    pointed at them), CanSee, the observer and target states; the sound module's listeners (now with the callback:
    each side's own OnSoundHeard); the controller's virtuals; the body; the actors module; the team; the scripted
    controller's Suspend; the remembered-sound pool (the original's free list read back after ForgetOldSounds);
    timeGetTime (the port's import slot and the original's IAT entry). IsFriendOf, IsDisguisedEnemy and
    UpdateDisguise (part 21's, tested there) are scripted per actor, the port's through the linker's --wrap.
  - Actors: three other humans, a car, a meleeTracker (standing for a vehicle that is not a car too), the human.
- **The tests (all exact):**
  - 24a: the seen lists (add, remove, HasSeenEnemy: the human or the others, once or twice in a list), OnSightEvent
    (lost or seen or neither, each kind of target, the human itself, deaf, no controller, the meleeTrackers for the
    player's soldiers), UpdateSeenHumans (dead enemies, traitors hostile now, watched or not), OnSoundHeard (each
    chapter and unknown ones, each source or none, at the edges of the hearing range, at the human, 1 m away, a
    remembered sound refreshed, deaf, just made noise, a car or another vehicle, no listener, a frame without data
    or without an actor), ForgetOldSounds (about 3 s old), Stop/StartListening, OnProximity, ProcessEvents (the
    controller hands back the event, none or another), RaiseEvent, Freeze (hidden seats, the active player), Suspend.
  - 24b: Reset, Tick, the nearby actors, AddUnknownActor, Raise, PopFront, AddStimulus (the same actor or source
    tracked already, keeping the priority or not, a shorter time), GetStrongestStimulus (at an event's place),
    GetAttacker, SetSniper, ActorToId, IdToActor, SaveGame (files byte for byte), LoadGame (saved files and random
    ones: every chunk kind, unknown and short ones; the loaded events compared by what LoadGame sets, as each side's
    pool hands out its own objects).
- **Kept out:** what the original does not survive and the port checks: a sight event of a target that is not
  registered (OnSightEvent, and UpdateSeenHumans through a seen human's target), a footstep with no source (the
  original reads its type), a speech whose source is not a human (it reads its health).
- **Not compared:** the debug strings of a sound without a listener; the original's GetMoveSpeedClass of a footstep's
  source (a call the port leaves out: the original drops its answer; the sources' controllers have no move flags,
  so it asks nothing more).
- **Port fixes:** S_HumanEvent::LoadGame and C_HumanEvents::LoadGame returned true always; the original returns
  whether an event chunk / the sniper chunk was read (no caller uses it).
- **Results:** 174,901 checks; lines: human_senses.cpp 100 % of 194, human_events.cpp 100 % of 204.
- **Mutations** (`human/mutations/mutate_senses1.py`, `mutate_senses2.py`; mutrun.py with MUT_BAT build_senses.bat,
  MUT_DEFS as the bat's -D options, the bat's --wrap options in MUT_LDFLAGS, MUT_TIMEOUT 120, SENSES_ONLY=24a / 24b):
  - 24a: 154 mutants (149 generated, 5 for repeated lines); 136 killed, 14 not building, 4 survived (equivalent:
    EraseAll's unread count, the debug strings).
  - 24b: 157 mutants (115 generated, 42 for repeated lines); 146 killed, 7 not building, 4 survived (equivalent: an
    equal time, a dropped read, the scores scaled alike).
- **Harness lessons (my mistakes):**
  - My fakes were named like part 20's (a redefinition): this part's are SN_*.
  - A seen human whose sight target was not registered made the original read through null (UpdateSeenHumans):
    my check read past the target table; such humans now get a registered target.
  - The original's event pool had no block size (its constructor never ran): the first load that emptied it crashed.
  - The port's loading left events it released in its pool, and later loads handed one out twice: the pool is now
    drained down to a sentinel after each load.
  - First mutation passes: the CanSee fake always answered, Freeze's frame Update was not seen, short id chunks and
    source id 0 were rare, and S_HumanEvent::LoadGame's result was never looked at.

## Human part 25a (the crowd's geometry and a memory's steps, `human_crowd.cpp`, 2026-11-14)

- **The build:** `human/build_crowd.bat` (HUMAN_CROWD, `crowd_test.inc`): part 24's harness with human_crowd.cpp linked.
  - The AI memories (C_HumanAIMemory, the same layout on both sides) are each side's own: their owner (the human or
    one of part 24's other humans) and route (the port's std::vector, the original's MSVC6 vector of the same
    waypoints) differ. After each call both memories are compared word by word (floats within the usual tolerance;
    bit for bit with CROWD_EXACT=1 since part 25c, see there), pointers by what they name.
  - The checkpoint network is the test's: 40 points about the humans (every fourth on whole metres with a radius of
    whole halves, for exact ties), linked to their nearest few. Both games point at it (the original's +0x54); its
    count and points are at the original's offsets. Get and FindNearestPoint (checkpoints.cpp is not linked) are
    fakes on both sides; the original's Get takes a 16-bit index (KeepOnNetwork passes a link's target in AX).
  - The original's crowd methods get a zeroed crowd object as `this`.
- **The tests (within the float tolerance when written; bit for bit since part 25c):** AddPush (no total, along,
  against, across), KeepInsideStrip (in either circle, beside, before, after; exact ties on the edges), KeepOnNetwork
  (in, out, along a link, no point, on the edge), SortCorners (in and about the unit square, no direction),
  AdvanceWaypoint (a move ending about 5 cm short),
  MoveOneStep (a route or none, walking or not), PushFromHuman and PushTargetFromHuman (standing or lying, overlapping,
  on a collision course, the same target, a tiny offset), PushOntoNetwork, HoldOnNetwork, TurnPushAside (pushed straight
  back), UpdateStuckTimer, the memory's SaveGame (files byte for byte) and LoadGame (saved and random chunks, the
  chunk after it read next).
- **Not compared:** the w of the memory's position, velocity, push and target (+0x2C, +0x3C, +0x4C, +0x6C): the
  original's HoldOnNetwork, MoveOneStep, MoveTarget store a stack temporary's undefined one, and its AddPush copies a
  whole 16-byte push in.
- **Port fixes:** UpdateStuckTimer summed the squares in another order than the original's ((z^2 + x^2) + y^2): an
  exact tie with the best distance went the other way. PushFromHuman asked for the other's stance only after the floor
  test; the original asks first (no effect: the calls now agree).
- **Results:** 60,653 checks; lines: 95.9 % of these functions' 386 (the rest: the car helpers, PointRate and
  IsInvalid, part 25b's, and FirstRootAhead's both-roots-behind case, unreachable).
- **Mutations** (`human/mutations/mutate_crowd1.py`, regenerated after part 25b): 415 mutants (396 generated, 19 for
  repeated lines); 335 killed, 19 not building, 61 survived: 21 in helpers 25a does not call (9 killed by 25b's
  tests, the rest part 25c's), 18 equivalent, 22 float ties at exact equality. Rerun exact in part 25c (see there).
- **Harness lessons (my mistakes):** my network Get fake took the whole index word (the original passes 16 bits);
  `near` is a windows.h macro (again); I aimed a human exactly at another for a mutant and made the side it gives way
  to rounding noise; the original's undefined w words.

## Human part 25b (the crowd's planner, `human_crowd.cpp`, 2026-11-14)

- **The build:** part 25a's (`human/build_crowd.bat`, `crowd_test.inc`, CROWD_ONLY=25b for these tests alone).
  - Four memories now: the human's and the three other humans' (each one's +0x2C4 points at its memory).
  - The crowd object: the port's g_humanCrowd; the original's its own, in MSVC6 form: the car vector at +0x10 (its
    slack random), the cars' half sizes at +0x20 (16-byte elements: its loops index them by i << 4), the scratch vector
    at +0x30, its node array (+0x40, capacity +0x44 = 200) and result array (+0x4C) the test's. Before each call the
    port's nodes (node pointers to the original's array, marks kept; avoided humans and car to the original's) and
    header scalars are copied in; after it both node arrays, results, scratch lists and header are described alike and
    compared (nodes by index).
  - The others' controller (any type, the scripted one's suspended flag, Unknown108's answer), two crowd cars (frames
    turned, some tilted, standing or moving; the port's private speed found by probing GetSpeed), the waypoint pool
    (AllocWaypoint logged; the released ones read from each side's list), the events rebuilt for the original before
    each call.
- **The tests (within the float tolerance; CROWD_EXACT=1 made only the nodes' and results' float hash exact, not the
  memories and the crowd's floats as first written here; all bit for bit since part 25c):** FindPointNear (no start, a
  hint: none, the start, another, a linked neighbour with the place between or beyond the two; ladders not gone
  through), IsPointBlocked (near the others, standing or lying, on other floors; near the cars, the human's own
  vehicle), FindBlocker directly (from the human to its waypoint: humans standing or lying, cars, nodes with no point),
  Replan (whole plans: ExpandNode, AddSideWay, CollectBest; the root's and goal's old contents filled first).
- **Not compared:** the w of node positions and of new waypoints' positions (the original's corners and goal take a
  stack temporary's).
- **Port fixes** (the first four found with CROWD_EXACT=1: the last bits decided which side of a blocker a plan went):
  - AddSideWay/ExpandNode: the original's ExpandNode returns at once when a node is wanted with the node array full;
    the port filled the array and went on with the other side and the other children. AddSideWay now says so.
  - S_Ellipsoid::Local: the original multiplies by the reciprocal of the half size, summing z + y + x; the port divided.
  - AddPush: the original's dot product is (x + z) + y in its own operand order; the port's x + y + z.
  - FindBlocker's car corners: the original scales (v * half) * 1.05 per axis; the port scaled the corner first, (v * 1.05) * half.
- **Results:** 103,471 checks (25b alone); lines: 99.5 % of 833..1386 (not run: 1147, a node with no point near it,
  and 1219, a negative discriminant only rounding reaches).
- **Mutations** (`human/mutations/mutate_crowd2.py`): 381 mutants (319 generated, 62 for repeated lines); 311 killed,
  21 not building, 49 survived: 14 equivalent, 7 not reached (rare plan shapes, narrow distance windows), 28 float
  ties at exact equality. Part 25a's list regenerated for the changed file (its helpers Local and CarEllipsoid: all 9
  killed by these tests). Rerun exact in part 25c: the same 49 survive.
- **Harness lessons (my mistakes):** the original crowd's vector offsets off by 4; its events never rebuilt for the
  crowd calls and its nodes never copied in at first; the car half sizes need a 16-byte stride; the original's node
  capacity (+0x44) left 0 (it returned early or crashed); my header copy overwrote its result pointer (a crash); the
  mutation baseline then failed under mutrun's SSE math.cpp (real last-bit ties, which led to the order fixes above).

## Human part 25c (the crowd's tick, `human_crowd.cpp`, 2026-11-14)

- **The build:** part 25b's (`human/build_crowd.bat`, `crowd_test.inc`, CROWD_ONLY=25c for these tests alone).
  - CROWD_EXACT=1 now makes every comparison bit for bit: the memories (MemDiffers), the waypoints the crowd reads, the
    crowd's floats and new waypoints, the planner's float hash. Until now it made only the hash exact (25a's and 25b's
    sections said otherwise: corrected there).
  - Not compared: the w words the original fills from stack temporaries (the memory's +0x0C, +0x2C .. +0x8C and +0xCC,
    the waypoints' positions' w; AllocWaypoint's logged position without it).
  - The controllers' GetWaypoints (slot 0xD4: route 0 for the human's, route 1 for the others'), the body's climb and
    work state 5 answers, the game role (synced to 0x982100), the profile's co-operative game and Unknown104's answer
    are fakes or fields set on both sides. A route pointer in a memory is compared by which route it names.
  - Each tick test randomises the controller types, move flags and the scripted one's suspended flag, healths, riders,
    velocities (the humans' and the cars'), routes (or none), the routes' start and last goal (now and then the quiet
    NaN markers, either sign) and the frames: turned about y, tilted, straight up or down, nearly so with x tiny, or x
    exactly 0 with z tiny; sometimes up to 0.3 m off the memory's position.
- **The tests:** PushFromCar and PushTargetFromCar (the memory and its target at a car, its centre, a hair off it along
  x or z, or about it; moving cars), StepAlone (alone, near others or cars, the work state 5 body, the player's type;
  now and then a hair from the route's last waypoint), Prepare (all controller types, routes or none, lying), Tick
  through HumanPass_Tick (random active lists: humans, the crowd's cars, the meleeTracker, in any order; the crowd's
  lists of humans and cars compared too). 24,000 calls; all bit for bit with CROWD_EXACT=1.
- **Port fixes** (all found by the exact comparisons; the last bits decide ties at the push edges and plan sides):
  - SideStepEllipsoid (25a): the original sums from z (z + y + x) for the speed, the dot and the human's miss distance;
    the car's squares the miss distance from x; the tangent term in the original's order. Now a flag tells them apart.
  - PushFromHuman (25a): the lying overlap ((z^2 + y^2) + x^2) - 1; the standing across square (y^2 + z^2) + x^2.
  - PushTargetFromHuman (25a): the lying case has its own sphere coordinates (divided by the half sizes, z y x, y z x,
    y z x) and square (z^2 + x^2) + y^2; on top of each other the dots go z y x and NormalizedZero takes the caller's
    square ((z^2 + x^2) + y^2); standing, the near-zero case's square is (x^2 + z^2) + y^2.
  - The car pushes: each works out its own sphere coordinates (PushFromCar by reciprocals, x y z, x y z, y z x; its
    relative velocity's x z, y times 0; PushTargetFromCar divided, z x y, z y x, x z y) and squares in its own order.
  - CollectBest (25b): its cost ((z^2 + x^2) + y^2) + cost and distance (z^2 + y^2) + x^2.
  - Tick's last loop: the target test's square (x^2 + z^2) + y^2, the move's (z^2 + y^2) + x^2.
  - Prepare: Unknown104 is asked even when the human is not walking (the original's &; the port's && skipped the
    call); the lying facing is normalised as GetLocalDir does (square y z x) before it is flattened.
- **Results:** 67,189 checks (25c alone; 25a 60,519, 25b 103,471, all exact, 0 failures); lines: 291 of the 292 in
  25c's ranges (not run: the one-argument NormalizedZero, used by the script module's crowd test only).
- **Mutations** (`human/mutations/mutate_crowd3.py`, all three lists regenerated for the changed file and run exact):
  319 mutants (275 generated, 44 for repeated lines); 281 killed, 8 not building, 30 survived: 2 killed by 25a's
  tests, 19 equivalent, 1 not reached (a lying facing exactly vertical), 8 float ties. Part 25a: 436 mutants, 349
  killed, 22 not building, 65 survived (19 killed by 25b's or 25c's tests, 23 equivalent, 23 ties); part 25b: 382, 312
  killed, 21 not building, the same 49 survived.
- **Harness lessons (my mistakes):** I reported 25a and 25b as bit for bit when CROWD_EXACT compared only the planner's
  hash exactly; the memories and the crowd's floats were within the tolerance, which hid the order differences above.
  I misread PushFromHuman as keeping the offset's y (my search missed `MOV [ESP+n],0x0` stores; the original zeroes it).
  I had seen Tick's last-loop orders and not applied them until two mutants showed it. A split debug mode for Tick
  crashed and mapped pointers wrongly (removed). I changed SideStepEllipsoid's signature without updating the script
  module's crowd test (the suites caught it: it did not build).

## Human part 26 (the body forwarding to the work, movement and equipment, `human_body.cpp`, 2026-11-14)

- **The build:** `human/build_bodywrap.bat` (HUMAN_BODYWRAP, `bodywrap_test.inc`): the base harness (build.bat's: the
  human, its movement and work with the original's copies in the original's layout, DiffCall) with human_body.cpp
  linked. The other human builds fake S_HumanBody, so this one is its own.
  - Every out-of-line method human_body.o calls (the work's, the movement's and its morph, idle and animation player,
    the equipment's, the table lengths, the camera module's: 56) is wrapped with ld --wrap (`bodywrap_wrap.rsp`) into
    a logger; the original's same functions are redirected to the same loggers. The inline parts run on each side's
    objects (the work's flags and Die, the movement's flags, ResetMovement's fields), which DiffCall compares after.
  - The loggers that matter to the state act on it alike: ClearFlags clears the movement's flags, the work's Interact
    sets its refused requests, Play logs the work's m_forced, the saves write and the loads read a tag.
  - The original's SetModelVisible calls the model (movement +0xA9C, slot 0x30) itself: a fake model logs it under
    the movement's name. Its camera module (slot 0x70) is the test's.
  - Create and Destroy run on bodies of their own: the original's _aligned_malloc gives the test's blocks by size, the
    port's new objects are named by its fake constructors (0xCD-filled movements on both sides).
- **The tests:** every body function with random roles (local, client, server, both), the human local or
  not, simulated or not, carried or not, the work's stance, gait, mode, refused requests and flags, the movement's
  flags, state (the climbing ones), ladder, timers, light, ground, sector and velocity (zero, tiny, exactly 1, any);
  the commands with every kind and out-of-range ones; DoAction's actions and out-of-range ones; poses -2..10;
  interactions 0..16 with flags; SaveGame (files byte for byte) and LoadGame (saved files, the chunks reordered,
  repeated, missing, without their data; inside a parent chunk with one after it, read next on both sides).
- **Not tested:** LoadGame with an unknown chunk inside the body's. The original does not leave it, reads on at the
  garbage headers past it and, once the file ends, loops for ever (it stops only at exactly 0 bytes left, and a
  failed read keeps the last id); the port's loop stops at the end. The game never writes such a chunk.
- **Results:** 63,750 checks; lines: 271 of 271.
- **Mutations** (`human/mutations/mutate_body_wrap.py`): 185 mutants (164 generated, 21 for repeated lines); 166
  killed, 18 not building, 1 survived (Destroy's `delete work`: a leak only).
- **Harness lessons (my mistakes):** the fake constructors first returned nothing (the original's Create keeps what
  they return); LoadGame's file had no parent chunk, so its loop never ran while all its comparisons agreed (coverage
  showed it); the first crafted files with unknown chunks hung the original (above); Create's write to the new
  movement and PlayDeath's m_forced were not compared until mutants showed it.

## Human part 27 (the human's state for a joining network client, `human_net.cpp`, 2026-11-14)

- **The build:** `human/build_netstate.bat` (HUMAN_NETSTATE, `netstate_test.inc`): the base harness with human_net.cpp
  and net/bitstream.cpp linked, part 26's way.
  - The bits go through each side's own bit stream code (WriteBits, ReadBits, WriteString, ReadString) on port
    C_NetMessage objects over the test's buffers: the port's C_BitStream has the original's layout, and the original's
    WriteBits reaches the port's Reserve through slot 0. Written words and bit positions are compared.
  - What the two functions call besides (18 functions: the soldier state's getter, serialisers and Setup, the
    position, the human's IsAISuspended, IsSurrendered, IsUnconscious, Freeze, SetTexture, PlayAnimation,
    ApplyRemoteHit, ExecuteBodyCommand, the actors module's GetActorById and ClearActorContainers, the controller's
    RaiseWeapon and LowerWeapon) is wrapped (`netstate_wrap.rsp`) on the port's side and redirected on the original's,
    to the same loggers. The soldier state's serialisers stand in with a format of their own (its health in 8 bits, a
    5-bit marker); the position goes as three raw floats (the original's GetSender, WriteQuantized and its inline
    reader at 0x005aa740 faked).
  - The vehicles are fakes of each side's layout and vtable (car, submarine, static weapon, Junkers 88, a door, a
    human; FindSeat and SetSeatBlocked logged). The controller is two blocks: the port's class's layout and the
    original's offsets (type +0x48, raised weapon +0xAD, animation +0x150, frame +0x188; the port's differ).
  - The original's operator new and delete (its C_str copies) use the test's heap.
- **The tests:** WriteState (20,000: in every kind of vehicle or none, players and others, dead or alive, faces and
  animation names of every length, the controller's types, raised or not, suspended, surrendered, unconscious);
  ReadState (20,000: random input in WriteState's format, vehicles found or not, the position read or not, faces,
  animations, the flags; the human's fields, the frame's position bytes and the bits read compared).
- **Results:** 80,000 checks, 0 failures; lines: 80 of 80. No port fixes needed.
- **Mutations** (`human/mutations/mutate_netstate.py`): 96 mutants (89 generated, 7 for repeated lines); 86 killed, 6
  not building, 4 survived (seatWritten, a variable set and never read).
- **Harness lessons (my mistakes):** I called WriteState through the human's fake vtable (it is virtual); I set the
  controller through the port's members for both sides, but the port's layout is not the original's (the original saw
  type 0, so its AI branch never ran while the calls agreed); that branch then crashed in the original's CRT heap
  until its operator new and delete were redirected. P_Unexpected now prints its caller's link-time address.

## Human part 28 (the speech sets and sound codes, `human_speech.cpp`, 2026-11-14)

- **The build:** `human/build_speechsets.bat` (HUMAN_SPEECHSETS, `speechsets_test.inc`): the base harness with
  human_speech.cpp linked (no wraps). The original's objects are built in its own layout from the port's (a sentence
  0x1C bytes: its MSVC6 vector's first, last and end at +0x04..+0x0C, the cursor an iterator at +0x10, the next time
  +0x14, enabled +0x18; a set 0xF24 bytes; the sets' list first and last at +0x04 / +0x08, its C_chunk at +0x10) and
  both described as text after every call. Both sides draw from the same rand() (the original's _rand redirected, both
  seeded alike); the original's new and delete use the test's heap, its file opens the fake engine's.
- **The tests:**
  - The sound codes (the original's fastcall functions) on every code 90000000..99999999 and 2,000,000 random ones;
    SetSet and SetVariant on 400,000 codes, values in and out of range.
  - AddSample (40,000 sentences: empty or not, any cursor, vectors with room or not); Say, SayNow and SetDelay on
    sets (4,000 x 40 calls: any sentence, out of range too, delays below 1, 1 and more, the mission time at the
    sentence's next time now and then).
  - Acquire, Release (sets handed out, sets the list does not hold) and Clear on Tables\speech.set: the game's (H:\
    HD2_Unpacked) and the test's (any number of sets, names, sets without sentences, sentence ids out of range,
    sample chunks short of 4 bytes, the sentences before the name, a wrong top chunk, no file). The file is rewritten
    between the two sides' calls. ResetDelays (inline in the original's C_game::RestartMission, no function to call)
    against what it should do.
- **Port fixes:**
  - SpeechCode_GetSet and GetVariant computed in doubles; the original multiplies on the x87 in the game's 24-bit
    precision (the product rounded to a float). 6,499 of the ten million speech codes had another variant (90010000's
    is 1, the port said 0; 90030000's 2, the port 3) and 109 another set. Now rounded to a float as the x87 does: all
    agree.
  - Acquire's sample: the original keeps one variable for the whole load (a short sample chunk repeats the last
    sample); the port started each at 0.
- **Results:** 784,380 checks (25 s; mutation runs 7 codes in 1); lines: 130 of 130.
- **Mutations** (`human/mutations/mutate_speechsets.py`): 114 mutants (97 generated, 17 for repeated lines); 92
  killed, 6 not building, 16 survived, all equivalent (dead stores, leaks, start values never read, the unused name
  size, Ascends and a Close after the set is made).
- **Harness lessons (my mistakes):** the port's Acquire ran first on the shared file, so a mutant opening it for
  writing emptied it for the original too and both agreed on nothing loaded; short sample chunks first gave the
  original's undefined start value (now only after a full one in the same set, counting only sentences it reads).

## Human part 29 (the humans' animation tables, `human_anim_tables.cpp`, 2026-11-14)

- **The build:** `human/build_animtables.bat` (HUMAN_ANIMTABLES, `animtables_test.inc`): the base harness with
  human_anim_tables.cpp linked; `animtables_globals.cpp` stands in for g_animationCache (only its address is used).
  - Each side makes, loads and frees its own tables (the original's in its globals 0x0098a474..0x0098a4a4). The
    port's tables keep the original's layout: compared word by word (animation sets by name, the records pointer as
    null or not).
  - The animation cache is a fake on both sides (the port's C_ResourceCache::Get wrapped, the original's
    AnimationCacheBase_Get redirected): one fake set per name (COM-style vtable: Release logged, GetEndTime from the
    name), a name that cannot be a file's failing as the real cache would. ShowMessage logged on both sides.
  - The original's new and delete use the test's heap and this build's operator new[] is the test's: new blocks
    filled with 0xCD on both sides. The fake engine's dtaRead can now fail (-1) and its dtaOpen records its flag.
- **The tests:** 160 loads: the game's Tables\anim.sav, the M1919 front sights mod's (a copy; the full size), the
  test's (flags, times, weights, names of 0..19 characters, control characters, a leading space; longer and shorter
  files), no file, a failing read; Init, Done, EnsureHumanAnimTablesLoaded (once, its flag set for good), SaveRecords
  before a load (the size, then the records); after each load the length queries (the carry tables' and the activity
  table's, stdcall in the original, and the movement tables' through the original's 0x0075d000 on a fake movement
  whose human's work names the group): 3,000 each, ids in and out of range.
- **Findings (no port fixes):**
  - The game's anim.sav is 111,624 bytes; the tables take 111,708 (seven carry tables). The last 84 bytes, the seat
    weapon table's last records, are what the heap block held, in the original as in the port: with 0xCD they make
    names neither side can load.
  - The original's carry tables leave their slots' weights, its activity table its slots' times, unset until loaded;
    the port's start at 0 (zeroed in the original's copy for the comparison).
  - The original's DoneHumanAnimTables leaves its globals pointing at the freed tables; the port's clears them.
  - The port's ReadName stops a name at its 20 bytes (the original reads on); no name in the game's or the mod's
    file fills them.
- **Results:** 2,329,289 checks, 2 s; lines: 198 of 198.
- **Mutations** (`human/mutations/mutate_animtables.py`): 156 mutants (106 generated, 50 for repeated lines); 102
  killed, 29 not building, 25 survived, all equivalent (14 frees and closes, 4 stores into objects being freed, the
  2 globals the original leaves too, the carry tables' unused index, a min of equals, the 20-byte names' terminator).
- **Harness lessons (my mistakes):** I first compared the tables after DoneHumanAnimTables (the original's globals
  dangle: a crash); my fake cache made a set for every name, so the 0xCD names of the short file (read 20 bytes by
  the port, on into the heap by the original) differed where the real cache fails on both; the uninitialised fields
  of the original's constructors needed zeroing after every Init, not only the first.

## Human part 30a (the stain manager and its mesh graphs, `human_stains.cpp`, 2026-11-15)

- **The build:** `human/build_stains.bat` (HUMAN_STAINS, `stains_test.inc`): the base harness with human_stains.cpp
  linked (the port's I3DGetDriver wrapped; GetIGraph and g_missionVariant the test's).
  - The engine's objects are fakes in its layout, every call logged: images (Release, Lock, Unlock; their size at
    +0x20), the IGraph's LoadTexture (slot 0x3C), the driver's states (0x18, 0x1C), materials (0x2C, 0x30, 0x34), face
    groups (Lock 0x0C, DuplicateMaterial 0x18; the material +0x04, the faces' count +0x08), mesh levels (vertices 0x04,
    unlock 0x08, face groups 0x14, 0x38, 0x3C; the UVs +0x4C, the vertices' count +0x54, flags +100), meshes,
    visuals (flags +0x1D0, mesh +0x1E0; 0xA0, 0xBC, 0xC4) and models (file name +0x134, FindChildFrame 0x48). The
    original's I3DGetDriver and GetIGraph (import thunks 0x007e529c, 0x007e530e) answer the same fakes.
  - Each side has its own world of them (built twice from one seed, the same names): the calls change them. After
    each call: the logs, both worlds, and the managers as text (the original's singleton 0x0098adf0 in its layout;
    its meshes with MSVC6 vectors: a vertex's edges at +0x08, a group's vertices at +0x10, edges at +0x20, the
    extremes at +0x30, +0x34, +0x4C, +0x3C, +0x6C, +0xAC).
  - Six models a world: four with meshes (two sharing a file name), one whose child is not a single mesh, one with
    no child. A mesh's seven face groups: 1..40 vertices, some at another's place, some at whole numbers, 1..2n
    triangles among them, some degenerate.
- **The tests:** 400 worlds x 30 calls: Init, Reset, Done, AcquireMesh (any model, none), ReleaseMesh (of meshes held).
- **Port fix:** BuildMesh asks the level for each face group twice, as the original does (for its faces, then their
  count); the port asked once (the calls compared).
- **Not tested:** a face group without vertices: the original reads the first vertex of none (a crash), the port's
  fix gives zero extremes. Real body meshes (none has met the stains in a test: still the synthetic ones, 11g's quads
  before).
- **Results:** 39,196 checks, 4 s; lines: 158 of the 169 in these ranges (not run: 142, unreachable; the static
  manager's destructor; Init's releases of images it cannot hold while not initialized; ReturnTextures, part 30b's).
- **Mutations** (`human/mutations/mutate_stains1.py`): 172 mutants (147 generated, 25 for repeated lines); 125 killed,
  7 not building, 40 survived: 13 equivalent (AddEdge's other-face handling repeats itself), 1 tie, 13 frees and
  sizes, 8 not reached, 5 for part 30b.
- **Harness lessons (my mistakes):** an empty face group crashed the original inside its own exception frames, which
  hid it from the harness's handler (a first-chance handler now prints where); I kept mesh pointers across an Init that
  deleted them, and a new mesh got a freed address on the port's side only.

## Human part 30b (the stained bodies, `human_stains.cpp`, 2026-11-15)

- **The build:** part 30a's (`human/build_stains.bat`, `stains_test.inc`), with bodies.
  - Each side's bodies: the port's C_HumanStains on a pose of a human (its classes' layouts), the original's (the same
    layout, by the offset comments) on a pose (owner +0, model +8) of a human (role +0x19C, containers +0x74), the
    container and item shared fakes (every vtable slot one function: the sides' slots differ). The bodies' fields
    (pictures by hash, textures by name), the images (releases, locks, pixels) and the managers compared after each
    call. Both sides draw from the same rand(); the game's time (the original's +0x2C) and g_missionVariant
    (0x0098a6b0) synced.
  - The worlds' meshes are surfaces now (bent grid patches: two triangles a cell, seams with UVs of their own, some
    1e-6 apart, whole-number places, degenerate triangles), the visuals turned and tilted; models of one file share
    their geometry. Images now and then of a 126-byte pitch.
- **The tests:** 80 worlds, 12 bodies each (on meshes, on a non-mesh child, on none; roles; items of 0..3 kg, exactly
  0.1 too; mission variants 0..2): every body bound and hit on the head in turn now and then (the 9th on takes the
  oldest borrower's textures), then 60 calls: Bind (modes 0, 1, 2, out of range), SetVisible, SetPaint (0..8), AddHit
  (at the mesh's places or anywhere, any direction, zone bits single, combined, with side bits), SaveGame (files byte
  for byte), LoadGame (its own or another body's save, another chunk, an empty file; a chunk after, read next),
  SkipSaved, ReturnTextures, Bind mode 2 on bound bodies; the destructors.
- **Port fixes:**
  - Bind mode 2 (another model) cleared the stains and made new pictures as mode 0 does; the original keeps them and,
    owning textures, takes them again for the new materials.
  - SetPaint with no paint put the head's overlay picture on a visible head; the original puts the null picture
    (SetVisible the head's or null, SetPaint the paint's or null: each its own).
  - AddHit summed the hit's local point x, y, z; the original (z + y) + x, and the front and side (y + z) + x.
  - The distances: the walks and searches (z² + y²) + x², NearestByWalks (z² + x²) + y², BuildMesh's joining test
    (z² + x²) + y² (the port: x, y, z or y, x, z). Part 30a's list rerun with them.
- **Not tested (the original would crash or read garbage; the game never does it):** a human without containers
  (Bind reads through null), ReturnTextures on a body without a level, the bodies before the manager's Init (the
  original finds its overlay and splat tables empty and does nothing; the port paints), a save of another picture size
  loaded into a body, a save loaded over a body borrowing textures, an image pitch above 128 bytes (the copies read
  pitch x height bytes of a 64 x 64 picture: past it, on both sides).
- **Results:** 78,233 checks, 18 s; lines: 574 of 586 in the file, 397 of 398 in 30b's ranges (not run: TakeTextures'
  return for a group without a face group or picture).
- **Mutations** (`human/mutations/mutate_stains2.py`): 457 mutants (418 generated, 39 for repeated lines); 397 killed,
  19 not building, 41 survived: 29 equivalent, 4 ties, 1 boundary not reached, 4 frees, 2 overruns not seen, 1 gap
  (the near seams only on one side along z). Part 30a's list rerun: 172 mutants, 127 killed, 7 not building, 38
  survived (13 equivalent, 1 tie, 14 frees and sizes, 10 not reached).
- **Harness lessons (my mistakes):** random triangles among any vertices made graphs whose searches take exponential
  time; two models of one file had different meshes (the manager keeps meshes by name: out-of-range vertices); the
  images piled up across worlds and hashing them took minutes; I read the bodies after their destructors; calling the
  manager's AcquireTextures directly left borrowers that did not know it, freed later; a 130-byte pitch, then
  loading over a borrowing body, made crashes that came and went (heap contents): the mutation runs with them were
  rerun.

## Human part 31 (the controllers' network messages, `controller_messages.cpp`, 2026-11-15)

- **The build:** `human/build_ctrlmsg.bat` (HUMAN_CTRLMSG, `ctrlmsg_test.inc`): the base harness with
  controller_messages.cpp and net/bitstream.cpp linked, part 27's way.
  - The bits go through each side's own bit stream code (WriteBits, ReadBits, WriteString, ReadString) on port
    C_NetMessage objects over the test's buffers; the written words and bit positions are compared, and both sides'
    Read get the same input.
  - The quantised floats and the 3D directions stand in on both sides with raw floats (the call logged with its
    sender or player index; C_NetMessage's own code is the net harnesses'). What the messages call besides (16
    functions: GetSender, the soldier state's serialisers, GetActorById, the human's FindItemByUid, GetFullName,
    GetControllerType, CreateEquipment and GetEquipmentState, GetGame and C_game::Print, GetItemDef) is wrapped
    (`ctrlmsg_wrap.rsp`) on the port's side and redirected on the original's, to the same loggers.
  - The item definitions: ten fakes, in the original's table at 0x0098b030 and behind the port's GetItemDef (the
    name at +0x10 on both). The inventory check's containers: each side's vtable (GetItem, GetUsedSlotCount,
    GetSlots) over the same items, the original's slots as pointers to MSVC6 vectors.
  - The message objects are the port's classes; the original runs on a copy of their bytes. The port's layout is the
    original's but where vectors are: the port's S_vector4 is 4-aligned, so Hit, ControlState, DropItem and Action
    have their fields elsewhere (moved by a table per message; since PR 23 the action command has the original's vtable too,
    its fields from +0x1C on going to +0x24 on). Compared word by word after each call, but for the vectors' w the original fills from
    stack temps.
- **The tests:** every message's Write (60,000: fields at random, edge floats, NaNs, names of every length up to past
  the buffer, bullets' speeds near whole numbers, containers full and empty, the backpack's contents missing) and Read
  (60,000: random words, or input in Write's format with edge values); WriteControlState and ReadControlState on their
  own, their results too (6,000).
- **Results:** 264,010 checks, 0 failures; lines: 368 of 370 (the two left: DropItem's clamps, which 4 bits cannot
  reach).
- **Port fixes:**
  - ReadControlState dropped a velocity under 0.01f; the original compares with the double 0.01, so 0.01f itself (a
    little under 0.01) is dropped too.
  - The action message read and wrote its raw float words through float values: an unoptimised x86 build passes them
    through the x87 stack, which quiets a signalling NaN (seen in the coverage build only). Now copied as words, as the
    original does.
  - The inventory check's print asked GetGame before the human's GetFullName; the original asks the human first (both
    getters: no effect, the order now the original's).
- **Not tested:** speeds of 2^31 and more in a hit (the original's __ftol keeps the low word of its 64-bit result, the
  port's conversion gives 0x80000000: no bullet flies so fast); an item type outside the definitions in the inventory
  check (the original reads its name through a null definition and crashes; the port prints none); the equipment
  message for a missing human (the original asks null for its equipment; the port sends the empty state); a belt
  container missing (the original calls through null). The soldier state's progress block: the original zeroes it,
  the port's S_SoldierProgress starts with its defaults (0.5, 1.0), but the items-only state neither sends nor reads it
  and CreateEquipment reads the items alone.
- **Mutations** (`human/mutations/mutate_ctrlmsg.py`, in the quick build): 368 mutants (293 generated, 75 for
  repeated lines); 339 killed, 13 not building, 16 survived, all equivalent or unreachable (DropItem's clamps, the
  (void) casts, a fifth byte copied past a float, a fourth float not read, MAX_OF_BITS[0] not read, a <= at the double
  0.01, 1023 clamped to 1023).
- **Harness lessons (my mistakes):** I first assumed the messages had the original's layout (the header's offset
  comments are the original's): the vector messages failed until the field tables; I copied the containers array's
  first element instead of its address (a jump into the original's data); my Hit velocities went up to 1e12 (the
  __ftol difference above); the full build made the mutation run take hours, so it was redone in the quick build.

## Human part 32 (the AI controllers' parts, `ai_parts.cpp`, 2026-11-15)

- **The build:** `human/build_aiparts.bat` (HUMAN_AIPARTS, `aiparts_test.inc`): the base harness with ai_parts.cpp
  linked. What it calls outside (the game, the road network's FindNearest, FindPath and Get, the car's
  SetUnknown4f2630, the actors module's GetActorById) is wrapped (`aiparts_wrap.rsp`) on the port's side and redirected
  on the original's, to the same fakes; the scene's FindFrame is a COM fake shared by both; rand, operator new and
  delete, _aligned_malloc and _aligned_free are the test's on both sides.
  - Each part is the port's struct and the original's copy of its bytes: the same layout for the facing, the orders,
    the alert state, the waypoints and the remembered sounds; S_AIFire's throwPos (+0x1C / +0x20), the tasks' own
    fields (+0x14 / +0x20: g++ puts them in the base's tail padding) and the driver's fields (by a table from
    `layout_offsets.py`, its path as an MSVC6 vector of 16-byte aligned storage) moved. Actor pointers go to each
    actor's original copy (net id +0x10, type +0x18); frames and road points are shared.
  - The original's 16 pools (0x008ab158) had no growth: their static initialiser never ran; the test sets each one's
    16. Its FindPath fills an MSVC6 vector<uint16_t> allocated with the test's heap. Its road Get takes the index's
    low word (the callers load only that).
  - Saved games go to files through each side's own C_chunk code (named by the process: the mutation runs run several
    at once), compared byte for byte; loads read the same file, then the objects and the next chunk are compared.
- **The tests:** S_AIFacing (40,000: SetView, SetFacing, LookToward, SetLookAround, TickLookTimer, the locks, the
  constructor, on facings of every kind, cones of 0.7, 0.94 and others, the locks set or not); S_AIFireOrder and
  S_AIMoveOrder (20,000 each: every setter and getter, the constructor, Wait's random time, positions past 100,000
  in y); S_AIAlertState::Set; C_AIDriver (30,000: SetVehicle, both DriveTo, Stop, ResetPath, PlanPath on random road
  networks: no path, the start or end not found, repeated points, points in line, a path already ending at the start,
  `beyond` NaN or a point); the saved games of the facing, both orders, the alert state, S_AIFire and the driver
  (1,200: saved by both, then loaded from the saved file or one made by hand: missing, unknown, repeated and long
  chunks, look-at frames found or not, no chunk at all); the twelve task types made by each side's pool, saved and
  loaded (1,200); the pools (30,000 allocations and releases of waypoints, raw waypoints, sounds and every task type:
  what each fills in, and which released object comes back).
- **Results:** 254,850 checks, 0 failures, 28 s; lines: 761 of 764 (not run: the driver's destructor, LoadGame's
  return without a chunk). No port fixes needed. The loads go into objects given other values first, and a chunk
  follows each part's in the files, so what a load does not set and where it leaves the file show. The driver's
  constructor is compared too (the original's leaves m_target, m_targetDir and m_label as they were).
- **Left out (the original's own undefined values, not read):** the vectors' w and padding its constructors leave;
  S_AIFireOrder's throwPos (its constructor does not set it); a new pool object's fields its allocation does not set
  (a raw waypoint's all, a waypoint's padding byte, task 2's unknown40); the tangent of a path of one point (the
  original reads past the path: the port's known fix); chunks shorter than their fields (the original reads stack
  garbage for what they lack); look-at names without a terminator (the original copies them as they are: the port's
  known fix). The hand-made chunks are therefore never short, so ReadIfFits' false branches are not taken.
- **Mutations** (`human/mutations/mutate_aiparts.py`, in the quick build): 695 mutants (507 generated, 188 for
  repeated lines); 62 not building; 586 of 633 killed; the 47 survivors unobservable, the port's lone-point fix, ties,
  initial values only a short chunk would show, and an Ascend the chunk reader makes up for.
- **Harness lessons (my mistakes):** my search for the car's top speed field zeroed every word before it (the cars'
  type and net id: their saved ids differed); the original's pools had no growth (its static initialiser never ran) and
  crashed the first allocation; the original's road Get takes the low word of its index (my fake read the stale high
  word as well); the saved games first took 7 minutes (Windows file I/O); fixed file names would have mixed the
  parallel mutation runs; the first mutation run's baseline failed on the fire order constructor's padding, so its
  "killed" meant nothing and it was redone; it then showed the loads ran into the objects just saved (nothing a load
  leaves out could show) and no chunk followed the parts.

## Human part 33a (the AI controllers' base: life cycle, small virtuals, body flags, saved games, `ai_controller.cpp`, 2026-11-15)

- **The build:** `human/build_aictl.bat` (HUMAN_AICTL on top of part 23's controller build, `aictl_test.inc`): the
  controller build with ai_controller.cpp, ai_parts.cpp (part 32's parts, real on both sides) and net/bitstream.cpp
  linked; part 23's controllers, human, actors, weapons and fakes and part 20's net property are set up as its own
  tests do.
  - Each side has its AI controller: the port's (a test class; its pure Unknown114 and ExecuteCommand, part 23's,
    logged) and the original's made by its own constructor (0x0056ee90, its parts too), with a copy of its vtable whose
    ExecuteCommand is logged alike. Before each call the port's is copied into the original's layout: the base by part
    23's table, the AI's fields by `layout_offsets.py`'s (S_AIFire's throwPos moved), the waypoints into an MSVC6 vector
    of the original pool's waypoints, the task into one of the original's pool (the derived fields +0x20), the parts'
    contents into the original's own parts (the driver's path as 16-byte aligned storage); after it the two are
    compared the same way, with the queued messages (part 23's describer, and the equipment's and command's).
  - The net object is part 20's block (enlarged to 0x100 bytes), each side's writes kept apart: the port's out-of-line
    setters write the original's offsets, as its inline ones do; the callbacks compared by address pairs, the contexts
    mapped. The queued net messages (the action and the dive) are logged by their fields from either side.
  - The AI's virtuals in other files (the senses, the combat, the movement), ApplyAlertState, the body's movement
    flags and climb target, GetActorPoints, SetUnknown4f2630 are logged on both sides; the original's g_netReplication
    is a block of the test's (its local id +0xDC); its pools get their growth (part 32). The message Reads are 33b's.
- **The tests:** 12,000 calls (SetAlertState, Attach in local and network games with and without a previous
  controller, on ladders, with and without a walking network; Detach, ReleaseTask, LookAround, LookAroundAt,
  SetMoveFlags, Get/SetFacingDir, Get/SetViewDir, OnCollisionEx on ladders and others, PlanGrenade, Unknown108,
  Freeze, OnBodyFlags with every throw in local and network games, IssueCommand, Drive in a car or not to a road point
  or a stop, GetPathPoints, RequestAction with weapons of any shot interval or another item); the saved games (2,400:
  saved by both, the files compared; loaded into controllers given other values from the saved file or one made by
  hand: the AI's chunks in any order, some left out, unknown ones); the destructor (400).
- **Results:** 36,402 checks, 0 failures, 34 s; lines: all 288 in 33a's ranges (300 of 394 in the file; the rest 33b's).
- **Port fix:** GetPathPoints left the current walking point's index as it was without one; the original writes 0xFFFF.
- **Left out, as the original's:** the ladder's points asked of a missing walking network (the original calls through
  null: the port's fix); flag 0x20000 without a hands item (the original's UseItem crashes on none); an interaction
  without a target in a save (the original reads its id through null); the tasks' vectors' w after a load, and a task
  2's human after a load naming another kind of actor (both what the pool's object had: the two sides' pools differ).
- **Mutations** (`human/mutations/mutate_aictl1.py`, in the quick build): 242 mutants (201 generated, 41 for repeated
  lines); 14 not building; 211 of 228 killed (with part 33b's queues for Detach); the 17 survivors are frees and pool
  releases nothing compares, an equivalent clear, a stale w and an initial value always read over.
- **Harness lessons (my mistakes):** I left out part 23's setups (combat, motion, deaths, interactions, weapons) and
  its second controller's copy, and first let the original's real ExecuteCommand run on random commands; the shared net
  object block was 0x40 bytes where the setters write up to +0x67; I tested a network game with
  `!(role & GAME_ROLE_LOCAL)`, which is true only for 0 (a null net object then crashed); my random actors did not
  follow part 23's ids (100 + the kind), so the two sides' GetActorById answered differently; the original's GetActorById,
  climb target and the hands' weapons needed their original copies; part 23's aligned-free logger counted the
  original's own vector growth.

## Human part 33b (the AI controllers' network side, `ai_controller.cpp`, 2026-11-15)

- **The build:** part 33a's (`human/build_aictl.bat`), the network tests appended to `aictl_test.inc`.
  - The messages' Read and Write are fakes of one format on both sides (part 31 compared the real ones): Write sends
    the type in 5 bits and the fields the controller reads, from each side's layout; Read reads them. The port's
    through its message classes, part 23's raw vtables' Write slot and the six Read wraps; the original's through
    redirects of its 23 Reads and 23 Writes. Queued messages are made on both sides (the original's with its classes'
    vtables), so its own deleting destructors free them.
  - CreateActorMessage(Ex) hand out the test's streams (C_NetMessage over buffers, each side's own WriteBits);
    SendActorMessage logs their bits. The net object's Tick, the net property's direction track (the original's at the
    property's +0xC0, SetDirection its slot 0x50), ApplyRemoteHit (wrapped) and ClientDropItem are logged alike; the
    multiplayer settings' game type set on both sides (the original's +0x224); the human has part 23d's speech set
    (the original's SayNow calls through a missing one: the port's fix).
- **The tests:** 12,000 calls: UpdateNetwork (co-operative games and others, in a seat or not, the dive with its
  stamina and moving conditions), FlushMessages to the server and to the clients (random queues of every type, the
  control state's unreliable stream, types 5 and above 23 skipped), OnMessage and OnMessage2 (random inputs in the
  fakes' format with types the switches skip); part 33a's Detach now flushes queued messages too.
- **Results:** 60,402 checks for the whole file, 0 failures, 38 s; lines: 394 of 394. No port fixes needed.
- **Mutations** (`human/mutations/mutate_aictl2.py`): 102 mutants (70 generated, 32 for repeated lines); 33 not
  building; 66 of 69 killed; the 3 survivors are OnMessage2's deletes of the messages it does not send on (frees are not
  compared). Part 33a's two Detach mutants are killed now (211 of 228 there).
- **Harness lessons (my mistakes):** my stream pool's Attach kept the old bit position (Attach does not reset it); the
  Say messages first ran without a speech set; the first mutation run's baseline failed because the runner lacked the
  bat's ApplyRemoteHit wrap, so its "killed" meant nothing and it was redone.

## Human part 34 (the remote controller, `remote_controller.cpp`, 2026-11-15)

- **The build:** `human/build_remote.bat` (HUMAN_REMOTE on part 33's build, `remote_test.inc`), with
  remote_controller.cpp linked; part 33's message and stream fakes, part 23's controllers, human and fakes.
  - Each side has its remote controller: the port's (a test class whose ExecuteCommand, part 23's, is logged) and the
    original's made by the base's constructor with a copy of its vtable (0x008112d8; ExecuteCommand logged alike), its
    own fields cleared, as the original's C_Human::CreateController does inline. The port's control axes and buttons,
    VIP and surrender flags (+0xAC.. in the port, +0xC0.. in the original) are copied over and compared, with the base
    (part 23's table), the queued messages and the net object's block (part 33a's way).
  - What the controller calls is logged on both sides: the client's item moves, Freeze, PlayAnimation (the original's
    loop flag pushed as the word holding the message's bool: its low byte compared), the movement's UseObject,
    OnRemoteAction and OnFrozen (wrapped: part 4's), the visual's SetDirection, the net game's player stats and rank, the
    human store's GetStats, the weapons' SetRounds and PlayEmptySound. The net object's Update (slot 2), the tracks'
    GetCurrent and GetRate (slots 10, 13) answer the test's vectors; PopMessage hands out each side's queue of net
    messages (action, dive, binoculars: the port's binoculars flag sits after the base's bool, +0x30 in the original).
  - GetSquadCommandGesture (player_controller.cpp, not linked) reads the original's table (0x0083c358): the remote's
    flow is compared here, the lookup with the player controller.
- **The tests:** Attach, Detach, OnBodyFlags, the three requests, FlushMessages and OnMessage (16,000: random queues,
  every message type but the inventory check; the shared message fakes now carry the command's, control state's,
  pickup's, heal's and animation's fields); Update (12,000: dead or not, a spectator, in play or not, in a seat, the
  replicated values, queued messages, net messages of every kind with every action, weapons, belt grenades).
- **Results:** 80,002 checks, 0 failures, 10 s; lines: 242 of 243 (the inventory check: the original reads its
  message's human unset, a stale value). No port fixes needed.
- **Mutations** (`human/mutations/mutate_remote.py`): 218 mutants (182 generated, 36 for repeated lines); 41 not
  building; 161 of 177 killed; the survivors are equivalent (7), a tie, values not compared here (the w copied from
  stack temps, the frame's on flag, the dive's controls Update clears, message releases) and the inventory check.
- **Harness lessons (my mistakes):** the shared fake still took the action command's field at its offset before PR
  23; I set the binoculars' flag at the original's offset in the port's message; Update's axes pointers were not
  mapped between the sides; the port's real OnFrozen ran on a movement the harness had not set up for it (a crash in
  UpdateHitSpheres); the original's real UseObject and ExecuteCommand first ran unlogged.

## Human part 35a (the AI controllers' senses: events, hits, sounds, distances, `ai_senses.cpp`, 2026-11-15)

- **The build:** `human/build_aisenses.bat` (HUMAN_AISENSES on part 33's build, `aisenses_test.inc`), with
  ai_senses.cpp linked; part 33's AI controllers, message and stream fakes, part 23's controllers, actors and weapons,
  part 21's other human.
  - The events the senses make are logged by their fields (type, time left, age, the ids, +0x30, the position) where
    they are raised or kept: C_HumanEvents::Raise and AddStimulus faked on both sides (the original's 0x005adac0,
    0x005adee0); the port's allocations hand out stale bytes, as a pool's; the original's event pool (0x008ac430) given
    its growth.
  - The human's seen enemies and friends are written into the original's human as MSVC6 vectors (+0x264, +0x274) on
    every copy; the sounds heard are each side's copy of one remembered sound (the source mapped); the definitions'
    carry mode and the actors' seat flags join part 23's fakes.
  - Part 33's comparison of the queued messages now compares every type by the fields the fakes' Write sends (it knew
    the equipment and command messages only).
- **The tests:** 20,000 calls: OnKilledEnemy, OnEnemySeen, OnEnemyLost, OnBodySeen, OnBodyHeard, OnFriendHeard,
  OnLoudSound, OnShotAt, OnSoundHeard (every chapter, around the human, the turns toward the sound), OnHit (local and
  network games, in seats with flags or seat weapons, the headgear and the weapons knocked away, dead or alive),
  GetDistanceTo, GetNearestEnemyDistance, GetAimDirTo, GetDirToFrame, GetTargetDistance2, GetViewDotTo, GetWeaponPos
  (components of every kind, the gun's "shdum"), FaceTowards, CanSeeHeadOf, GetNearestEnemy. Network games give the
  sounds human sources (IsFriendOf takes them as humans there, on both sides).
- **Results:** 40,002 checks, 0 failures, 7 s; lines: 236 of 416 for the file, every line of 35a's functions (the
  sight tests and the targets, 76-412, are 35b's).
- **Port fixes:** the sums in the original's orders (its inline Magnitude, Normalize and dot products, per site):
  Distance (z, y, x), GetAimDirTo's normalization (y, z, x), GetDirToFrame's, GetViewDotTo's and GetNearestEnemy's
  (z, y, x). The port's plain Normalize and Magnitude summed x, y, z: results differed in the last bit.
- **Mutations** (`human/mutations/mutate_aisenses1.py`): 236 mutants (213 generated, 23 for repeated lines); 34 not
  building; 189 of 202 killed; the survivors are equivalent (6), ties at exact boundaries (3), the directions' stale w,
  a read past the matrix and the ray's mask (the harness's volumes answer both).
- **Harness lessons (my mistakes):** the aim tests first took the copied other human, whose bones GetAimPoint cannot
  pick from, and CanSeeHeadOf its missing body frame (crashes); OnLoudSound met part 23's actors in network games;
  the event fake handed out zeroed memory and the queued messages were compared by type only, so the clears and the
  hit message's fields went unnoticed; the first stale pattern (0xA5) reads as a float near 0, within the tolerance; the
  sounds mostly fell behind the human and the knock-away paths were reached a few times only. The first mutation pass
  left two mutants that a rerun with no change killed: I rerun every survivor.

## Human part 35b (the AI controllers' senses: sight tests and targets, `ai_senses.cpp`, 2026-11-15)

- **The build:** part 35a's (`human/build_aisenses.bat`, `aisenses_test.inc`).
  - Three humans: the tested one, part 21's other and a third copy of it, each on its own frame around the tested one
    (also its body frame, the original's +0x260), with its own health, controller (the AI's with an AI type, or the
    second base controller as a player's), vehicle, seat and sight target id. The tested human's body frame is its own,
    turned (sometimes tilted, sometimes not turned).
  - The shared fakes answer per human when a test sets the new hooks (`g_humanAnswers`: unconscious, surrendered, view
    direction; `g_visiblePartsOf`: the sight module's parts by target id); other builds keep the shared values.
  - A car (the port's C_Car, its private +0x5E0 flag found through GetUnknown5E0) carries the seat weapons:
    HasSeatWeapon, GetSeatWeaponAim, GetSeatWeaponDef (a definition with a component of every kind) and
    CanSeatWeaponAim (answering by the enemy's position) logged on both sides. The camera module's view distance: the
    port's member and the original's slot 0x58. A MeleeTracker with its body frame (+0x544).
- **The tests:** 20,000 calls: TestSightOfHuman, TestSightOfMeleeTracker (the controlled actor now and then a
  MeleeTracker or another kind), PickTarget (with directions at the enemies or anywhere), PickTargetFromSeat; the test
  prints the outcomes (about 30 % of the sight tests see their target; PickTarget picks someone in 60 %).
- **Results:** 80,003 checks for the file's tests, 0 failures, 14 s; lines: 418 of 420 (two branches for angles above
  2 pi, which atan2 never gives).
- **Port fixes:** the original's sums in SightDirection (y, z, x), the MeleeTracker's range test, PickTarget,
  PickTargetFromSeat and the awareness score (z, y, x), its normalizations by the square already summed. Found by
  reading the x87 code, not by a failure: the scenes agreed before.
- **Not tested:** an AI controller of another type or a base controller other than a player's: the original reads
  +0xD0 of any controller as its fire target (a remote's own member there); the port answers none for those types.
- **Mutations** (`human/mutations/mutate_aisenses2.py`): 193 mutants (143 generated, 50 for repeated lines); 24 not
  building; 146 of 169 killed; the survivors are equivalent (12) or ties at exact boundaries (11).
- **Harness lessons (my mistakes):** the copied combat buffers were too small (the MeleeTracker's body frame at +0x544,
  the car's flag at +0x5E0); I set the car's flag at the original's offset in the port's car, where its C_Car reads
  another; the base controller first stood for remotes and the AI for any type, where the original reads +0xD0 (the
  differences looked like port bugs); the body frame was not turned, so the body's direction was always +z and
  SightDirection's x never mattered; my Python edit scripts' string literals turned the "\n" of two printf lines into
  line breaks (the build failed or the trace broke: fixed by hand).

## Human part 36a (the AI controllers' fighting: timers, aim spread, hit chance, looking, firing, stance, `ai_combat.cpp`, 2026-11-15)

- **The build:** `human/build_aicombat.bat` (HUMAN_AICOMBAT with HUMAN_AISENSES: part 35's build with ai_combat.cpp,
  `aicombat_test.inc`; part 35's humans, fakes and helpers, its tests not run).
  - Faked on both sides and logged: IsHumanWithin (ai_movement.cpp's, step 4), the human's CountAmmo, the sight
    module's CanSee, the body's GetCarryAnimLength. Part 33's fake of the original's ApplyAlertState is left out here.
  - The AI's controls (part 23's axes and buttons) are kept apart per side and compared (ShareBlock): before, both
    sides wrote one array that no test compared. The stance and speed class answer per human through part 35's hook.
  - UpdateLook: the original takes the w of the look direction it clears, and of the direction to a frame it looks at,
    from stack words no path sets; the facing's, view's and look direction's w (and the controller's copies) are the
    port's after the call.
- **The tests:** 24,000 calls: UpdateTimers, ExtendBurst, GetAimSpread, GetHitChance (each human its stance and speed),
  TurnTowards (on a ladder or not), ApplyAlertState, UpdateLook (frames, directions, the view locked, the look timer),
  StartFiring, StopFiring (weapons busy, the order's pause), PressReload, SetStance (every stance, carrying, special
  moves, animation 0x5F, the squad's print), UpdateCheckpointStance (points of every type, near and far).
- **Results:** 48,003 checks, 0 failures, 13 s; lines: every line of these functions (250 of the file's 452; the
  firing, aim and grenade are 36b's).
- **Port fixes:** the sums in the original's orders: TurnTowards on a ladder (z, x, y), GetHitChance's range, UpdateLook's
  dot with the facing and UpdateCheckpointStance's distance (z, y, x), UpdateLook's look direction (x, y, z); the fire
  direction's two tests in UpdateFireOrder (x, y, z firing, z, y, x not: one helper had served both). GetAimSpread and
  GetHitChance ask the stance again for their second test, as the original does (a getter: the calls only).
- **Mutations** (`human/mutations/mutate_aicombat1.py`): 278 mutants (246 generated, 32 for repeated lines); 10 not
  building; 243 of 268 killed; the survivors are equivalent (14), ties at exact boundaries (8), a stale w and a chance
  above 0.95 that five random bonuses would all have to reach.
- **Harness lessons (my mistakes):** part 23's weapon tests take the AI's controls away (a crash in PressReload); the
  shared controls were not compared at all; part 33's redirect of ApplyAlertState still stood (the original's side
  logged a fake); my item time was set on the port's weapon only; the move order's random flags were nearly always
  busy, so ApplyAlertState rarely got past them; a heredoc script with a Windows path failed (written with Write
  instead).

## Human part 36b (the AI controllers' fighting: the fire order, AdjustAim, ThrowGrenade, `ai_combat.cpp`, 2026-11-15)

- **The build:** part 36a's (`human/build_aicombat.bat`, `aicombat_test.inc`).
  - Part 23's weapons (each side its copy: time +0x14, attack time +0x58, ready time +0x5C (the port's found through
    GetReadyTime), flags +0x60) share part 23's definition and component; the component's virtuals sit at other slots
    on the two sides, so its vtable is each side's own during the call (GetReloadTimeMs: the port's slot, the
    original's 0x20). The definition's GetAmmoItemId and the AI's line-of-fire test (Unknown114: part 33's test class,
    the original's slot 0x114) answer the script on both sides and are logged.
  - After the original's call the facing's and the controller's w words are the port's: the directions the original
    hands SetView (ThrowGrenade's, GetAimDirTo's, GetDirToFrame's) take their w from stack words it does not set.
- **The tests:** 24,000 calls: UpdateFiring (rounds or none, reloading, targets the sight module counts or not, zombie
  and defensive AIs), CheckFireOrder (weapons busy or not ready, pauses, bursts ending, directions or targets, empty
  weapons without ammo, a client waiting for its weapon's aim, friends, the line of fire, dead targets), CheckReload,
  UpdateFireOrder (grenades, reload orders, firing or not, no target), AdjustAim (projectile weapons lobbing with the
  aim's error, others scattered by the spread and the difficulty), ThrowGrenade (standing, crouching, lying).
- **Results:** 96,005 checks for the file, 0 failures, 26 s; lines: 452 of 452.
- **Port fixes:**
  - AdjustAim and ThrowGrenade read the muzzle velocity from the component's field as the original's inline code does
    (the port called the virtual: another component kind would have answered 0).
  - AdjustAim's sums and roundings in the original's orders: the side vector's square (z, y, x), the side scaled by the
    spread before the random offsets, the direction's square (z, y, x), the aim error's z added doubled
    (tz + (e + e)).
  - LobDirection: the distance's square (z, y, x), the result's (y, z, x); its angle and cosine wider than a float
    (the x87's, in double here), the sine of the angle stored as a float, the square's y term the wide sine times the
    stored one.
  - NormalizeBy (controller_internal.h) sends a NaN square to the division, as the original's x87 test does (the port's
    zero-vector rule turned a NaN direction into -1).
- **Not tested:** a reload ordered with empty hands (the original reads the missing weapon's reload time, a crash the
  port fixes); fire-order targets other than the tested human and part 23's actors (the copied humans' models have no
  bones for GetAimPoint).
- **Mutations** (`human/mutations/mutate_aicombat2.py`): 205 mutants (177 generated, 28 for repeated lines); 10 not
  building; 181 of 195 killed; the survivors are equivalent (8), ties at exact boundaries (4), a stale w and the
  client's aim test, weak.
- **Harness lessons (my mistakes):** part 35b points the weapons' definition at its seat component on every call (the
  port called through a component without a vtable); the copied humans have no bones (GetAimPoint crashed); part 35b's
  visible parts could be only the extra bits, where the original's GetAimPoint divides by zero; AdjustAim was asked
  with empty hands (both sides read them); my reading of a test run cut AdjustAim's single differences off with
  `head`, so I first took them for fixed; a `sed -i` turned ai_combat.cpp's line ends to LF, which hid the repeated
  lines from the mutant generator (put back).

## Human part 37a (the meleeTrackers' controllers: life cycle, commands, alarm, Apply, timers, events, `melee_tracker_ai.cpp`, 2026-11-15)

- **The build:** `human/build_mtai.bat` (HUMAN_MTAI with HUMAN_AICOMBAT and HUMAN_AISENSES: part 36's build with
  melee_tracker_ai.cpp, `mtai_test.inc`; parts 33-36's humans, actors and fakes, their tests not run).
  - The controllers' layouts are the original's (the header's static_asserts, offsets checked): each side's controller
    holds the same bytes (the scripted and the automatic variants: the port's test classes; the original's made by its
    constructor 0x005cb190 with the variant's vtable, 0x00812250 / 0x008122c8), the tracker, the owner and the targets
    mapped, the behaviour weights' arrays each side's (the original's 0x008ac820 / 0x008ac720), the path an MSVC6
    vector of the original pool's waypoints (its AllocMeleeTrackerWaypoint, 0x005ce4c0; the pool's growth set: its
    static initialiser did not run). The original's operator new and delete are the test's (malloc and free).
  - The meleeTracker is a block per side: the port's C_Actor packs its vectors 4-aligned, so its fields sit elsewhere
    than the original's (the header names the original's offsets): the fields the controllers read are set alike (id,
    type, frame, position, direction, stats, the movement's speed target and air steps), those they write compared.
  - Faked on both sides and logged: the movement's SetMode, SetSubModeA / B, SetFacing, SetOptimizedController,
    CheckAttackSequence, GetSeqAnimA / C; the tracker's SetOptimized and SetController; the script's Advance and OnAlarm;
    the camera module's GetUnknown78 and SetViewDir (slots 0x78, 0x48); FindMeleeTrackerPath; the human's RaiseEvent;
    MeleeTrackerAnimClass; MeleeTrackerBarkTurn (the tracker in ECX); RouteAroundCars (vehicle part 4's).
  - The w words the original fills from stack words (the move accumulator's in Apply, the facing's in SetMoveToward,
    the two target positions' in ClearAttack and SetOwnerActor) are the port's after its call.
- **The tests:** 24,000 calls: Attach, Reset, Detach, ResetState, SetAlarm (both variants), SetOwner (no human),
  TakeChanged, Disable, Apply, the six mode commands, Suspend, SetOptimized, SetSightRange, SetHearRange, UpdateTimers,
  SubUpdate, DistanceToTarget, DistanceToOwnerSpot, OwnerPosition, ClearAttack, SetMoveToward (with the normalisation's
  snaps), OnHeardSound, OnSpottedEnemy, OnSeenHuman (both variants), ReleaseScript, SetScript, SetBarking; the base's
  and the AI's own members the variants override (Detach, SetOwnerActor, OnHeardSound, OnSpottedEnemy, Reset), called
  directly.
- **Results:** 46,255 checks, 0 failures, 16 s; lines: every line these reach but SetOwnerActor's and HumanOfFrame's
  with a human owner (FindPath: part 37c); 334 of the file's 1,084.
- **Port fixes:** DistanceToTarget's square in the original's order (z, y, x); the AI's OnSeenHuman documents its null
  check as a fix (the original reads a missing actor's type when ordered to; the scripted variant's checks).
- **Mutations** (`human/mutations/mutate_mtai1.py`): 274 mutants (217 generated, 57 for repeated lines); 9 not
  building; 249 of 265 killed; the survivors are equivalent (9), ties at exact boundaries (6) and SetBarking's result
  switched off (the original's is stale).
- **Harness lessons (my mistakes):** I took the tracker's layout for the original's because the header comments give the
  original's offsets: the port's actor packs its vectors (the first runs compared the wrong fields); I called the
  original pool's Grow for its Get (a crash); I faked MeleeTrackerBarkTurn as cdecl (it is thiscall); the variants'
  vtables never reach the base's overridden members, and the owners had no positions: the first mutation pass left 60.

## Human part 37b (the meleeTrackers' controllers: the update, targets, paths, sight, `melee_tracker_ai.cpp`, 2026-11-16)

- **The build:** part 37a's (`human/build_mtai.bat`, `mtai_test.inc`).
  - The tracker's lists are each side's: the humans seen (the original's copies mapped) and the sightings (a copy per
    side, the source mapped; the port's release is silent: the original puts the culled ones in its pool inline; the
    lists are compared). The original's clock is QueryPerformanceCounter / Frequency through its import slots
    (0x0080f1c8 / 0x0080f1c4): the counter is the test's microseconds at a frequency of 1e6, the port's MicrosecondsNow
    the same. FindMeleeTrackerPath hands each side the test's checkpoints (the original's an MSVC6 vector, malloc'd: its
    delete is the test's free). The tracker's velocity, last position and frame time, body frame, movement +0x364 and
    controller (+0x534) are set on both sides; a frame carrying a human (Attack, SetOwner) names each side's human
    during that side's call.
  - The path's waypoints are compared but for the two w words (the original's are stale stack words: the port's 1, its
    documented fix) and the padding (+0x27, +0x2C: the original's pool memory), its capacity too.
- **The tests:** 30,000 calls: Tick (the whole update; the port's static weights first filled with junk), CullSightings,
  DecideTarget (attacking a target near or far, held back or not, the timer's edges), DecideMove (an owner's spot about
  the tracker), FollowPath (the next waypoint within a metre, the line of motion passing it), AdvanceWaypoint (the last
  leg of a scripted MoveTo), CheckHeel, FacePoint, FindTarget, ScoreTarget, CheckReached, BeginAttack, Attack, MoveTo,
  SetOwnerActor and SetOwner, FindPath (with SmoothPath and the first point's drop), ClearPath, ResetPath, TestSight
  (humans by stance and pace, part 23's actors, the tracker itself); the tracker turned and tilted, targets in front
  within its sight range, a checkpoint by it.
- **Results:** 136,263 checks for the file, 0 failures, 19 s; lines: 852 of 1,085 (the rest: the scripted Tick's idle
  bark, the saved games, the remote and player controllers: part 37c; TestSight's no-target fix; mode 3's print).
- **Port fixes:**
  - RunPathSearch takes the checkpoints as int: the port cast FindNearest's -1 to 65535 for the search (the original
    passes the int on).
  - The original's sum orders: SmoothPath's projection (numerator x, y, z; denominator z, y, x) and distance (z, y, x),
    NormalizeXYZ (y, z, x), RunPathSearch's drop test (z, y, x), CheckHeel's spot test, ScoreTarget's distance (z, y,
    x), FacePoint's zero test (x, y, z), DistanceToTarget (37a).
  - SafeLength tests the float's bits as an int, as the original's inline code does (a NaN with its sign set counts as
    0); NormalizeXYZ divides a NaN square as the original does (the port turned NaN directions into -1).
- **Not tested:** the original's unchecked cases the port fixes: non-human or missing candidates for ScoreTarget (seen
  lists and loud sightings are humans here), a MoveTo without a script (the original advances whatever +0xC0 holds),
  CheckHeel without an owner (the original reads its spot unset), the sight test without a target or with a tracker
  without a controller; FindTarget's print (mode 3, no SetController variant).
- **Mutations** (`human/mutations/mutate_mtai2.py`): 679 mutants (631 generated, 48 for repeated lines); 72 not
  building; 516 of 607 killed; the survivors are equivalent (44: arrays' sizes, unread arguments, the scores' kept
  quirk, zero y terms, pool releases), ties at exact boundaries (34), mode 3's print and 12 weak ones (the pass test's
  line of motion, the predicted height, TestSight's range factors).
- **Harness lessons (my mistakes):** my random path array's capacity exceeded its allocation (the push overran the heap:
  crashes the harness could not report); a search that succeeded with no checkpoints (the real one gives at least the
  start); MeleeTrackerAnimClass takes its argument in ECX (I faked it cdecl); the follow flag set without an owner and
  the scored lists with non-humans (states the original does not survive); repeated checkpoints in a row (the smoothing
  divides 0 by 0: NaN paths, where the sides' NaN handling then differed); the source controller's weights mapped to
  the original's one global pair (overwriting the tested pair's copy); the waypoints' padding made the comparison
  differ from run to run; CheckHeel without an owner read the original's unset spot (the failures showed only in the
  coverage build's environment); a Python edit script again turned "\n" into line breaks several times.

## Human part 37c (the meleeTrackers' controllers: the idle bark, saves, the remote and the player's control, `melee_tracker_ai.cpp`, 2026-11-16)

- **The build:** part 37a's (`human/build_mtai.bat`, `mtai_test.inc`).
  - The remote (vtable 0x008121e0, 0x270 bytes) and the player's control (0x00812340, 0x4B0 bytes): each side's copy
    the same bytes, the tracker and the owner mapped, the weights' pointers into the copy's own buffers rebased (the
    test arrays the original's copies). ReadControlState (0x005ac660) answers the same state on both sides;
    WriteControlState (0x005ac5f0) logs the position, direction and bits (not the velocity: the original's player
    sends stack words there, the port zeros); CreateActorMessageEx / SendActorMessage are part 33's fakes.
  - The idle bark: the tracker frame has a "d" child (the mouth); _rand is the same rand() on both sides (part 33's),
    seeded alike before each side's Tick; the sound request is logged whole (part 13's).
  - The saved games: each side's into its own file through the port's C_chunk, the files compared byte for byte; the
    loads from the saved file or a hand-made one (the sub-chunks a random subset in a random order, unknown ones,
    another chunk id, now and then a closed chunk), the result, the chunk's depth and position after the load and the
    next chunk compared; GetActor answers the humans, part 34's actors and the tracker by net id on each side.
- **The tests:** 14,000 rounds: the scripted Tick with the bark due or not (the automatic one's Tick too); the saves
  and loads of the variants' slots, the full AI's own (0x005d25b0 / 0x005d28b0: no variant uses them) and the root's
  (0x005d2000 / 0x005d21d0); the full AI's Reset, Detach and Tick; the remote's and the player's Attach, Detach, Reset,
  FrameUpdate (each game role), Tick, OnNetState (the remote moves the frame: compared), save and load; the root's
  Attach; the player's OnNetControl, ResetState and MouseLook (zero and one-count moves, the pitch's limits); 400
  destructors.
- **Results:** 171,927 checks for the file (parts 37a-c), 0 failures, 30 s; lines: 1,087 of 1,090 (the rest:
  TestSight's no-target fix, 2 lines, and mode 3's print, which no variant reaches).
- **Port fixes:**
  - The scripted tracker's load returns false without the bark timer's chunk (none next, or another one, which is
    skipped), as the original; the port returned true (C_MeleeTracker::LoadGame passes the result on).
  - MouseLook: the arctangents, sines and cosines wide and each stored result rounded once, as the original's x87 code
    (the port rounded every step to float: the facing differed in its last bits).
- **Not tested:** the destructors' effect on the object (the compiler drops the port destructor's stores into the
  dying object: both sides' destructors run, nothing else is compared); a bark timer's sub-chunk shorter than 4 bytes
  (the original then stores a stale stack word: the game never writes one).
- **Mutations** (`human/mutations/mutate_mtai3.py`): 252 mutants (205 generated, 47 for repeated lines); 46 not
  building; 179 of 206 killed; the survivors are equivalent (21: array sizes, the load loops' size tests, a unit row
  normalised, the dedicated server's branch, MouseLook's moot second clamp), ties at the pitch limits (2), not compared
  (2: the destructor's stores, the loaded waypoints' w) and the two variants' mode numbers, which only their callers
  read (2).
- **Harness lessons (my mistakes):** I compared the frame's position after the player's OnNetState, which does not move
  it; my random paths' checkpoints (up to 300) crashed the load, which reads the checkpoint unchecked; the destructor
  test compared the object after the port's destructor (its stores are dead to the compiler); 5,000 saves made the test
  73 s (file creation is slow here: fewer saves, 30 s); the load test compared only the next chunk's id, which a missed
  Ascend does not change (the depth and position now); a bash heredoc again turned "\n" into line breaks.

## Human part 38 (the sniper AI, `sniper_controller.cpp`, 2026-11-16)

- **The build:** `human/build_sniper.bat` (HUMAN_SNIPER with HUMAN_AICOMBAT and HUMAN_AISENSES: part 36's harness with
  `sniper_controller.cpp` and `checkpoint.cpp` linked), `sniper_test.inc`, run_suites `human.sniper`.
  - The pair: the port's sniper (a test class: ExecuteCommand logged) is part 33's AI pair (AiCall converts and
    compares the C_AIController part); the original's is its AI constructor's object (0x0056ee90) with a copy of the
    sniper vtable (0x00811618). The sniper's own fields (+0x140 .. +0x180: the post, its stance, flags, task time,
    target; the two point vectors MSVC6 ones in the original) and the human's events (the sniper post, the stimuli: an
    MSVC6 deque at +0x48) are converted and compared around each call; the AI's control arrays (the port's statics, the
    original's 0x008ab058 / 0x008aaf58) too.
  - The private members reached through explicit instantiation (member pointers; no `#define private public`).
  - Faked on both sides: UpdateMove (step 4's), DrawLongGun / DrawSidearm, the sight module's TestNow, GetNeighbours,
    ProcessEvents, StartScriptedController, RequestThrowItem, GetActor / GetActorById (the humans and part 23's actors
    by net id); IdToActor (human_events.cpp's) and CoverAgainst (enemy_combat.cpp's, inline in the original) are copies
    of the port's until their files are linked. ReleaseHumanEvent is silent (the original puts the stimulus into its
    pool inline).
  - Part 36's per-side vtables (the weapon component's; now the definitions' slot 0x10 too: the port's GetCarryMode is
    the original's GetBehaviour) and its stale facing w words. The walking points are part 33's at the network's
    stride (Get and GetIndex agree; part 23's Get by the index's low word through a new hook, `g_checkpointAt`: the
    original pushes 16 bits). The next task of type 3 in both pools primed alike (a new task keeps its pool object's
    +0x0C and +0x10). The MSVC6 container helpers moved from `senses_test.inc` to `msvc_containers.inc` (shared).
- **The tests:** 20,000 calls: Attach (with a previous controller or none: the post's points and sniper points, the
  hands, the script's OnAlarmDone, the morale flags), Detach, Update, Tick (network games' send mask, queued messages,
  the task given back to the script), UpdateTimers (a target gone), DropHandsItem, UpdateFiring (an empty single-shot
  launcher thrown away), HandleEvent (each event type), UpdateTask (morale at its edges), CheckDone, RunTask, StartTask,
  UpdateZombie and UpdateSniper (every state), TestSightOfTarget, TakeCoverStance, HasSomethingToDo, SeesEnemies,
  SeesTarget, TakeStimulusSource, ChooseStance, FindCoverPoint, FindFirePointHeard / Seen, SaveGame (files byte for
  byte), LoadGame, Unknown114.
- **Results:** 62,163 checks, 0 failures, 17 s; lines: 414 of 414.
- **Port fixes:** none needed. Noted in the source: the turn's look direction's w is a stale stack word in the
  original (the port 0).
- **Not tested:** the original's crashes the harness avoids: a missing walking network (the original reads its ladder
  points; the port checks: part 33's fix) and missing checkpoints in UpdateSniper (both sides crash); aiming at the
  copied humans (their test models have no bones).
- **Mutations** (`human/mutations/mutate_sniper1.py`): 351 mutants (251 generated, 100 for repeated lines); 19 not
  building; 315 of 332 killed; the survivors are equivalent (12), ties at the turn's 0.76 (2), the silent stimulus
  release (1) and weak (2: the point at index 0, the tick's fire flags).
- **Harness lessons (my mistakes):** the original's container fakes had no IsLocked (an exit on "unexpected controller
  call"); part 23's Get read the original's 16-bit index as an int (a null point: a crash in the original's Attach); I
  mixed the two point tables (0x40 and the network's stride); the save needed an interaction's target (part 33's
  lesson again); the fire order unset (a crash in the port's UpdateFireOrder) and aiming at a copied human (no bones);
  the original's game object without the walking network (its +0x54); a load naming an actor I had made unknown (the
  sides' stale pool objects); comparing the 16-bit fire points as ints; a typedef cannot spell a member-pointer type
  (using), and `->*` binds looser than `->`.

## Human part 39a (the scripted controller: life cycle, alarm, moves, modes, objects, saves, `scripted_controller.cpp`, 2026-11-16)

- **The build:** `human/build_scripted.bat` (HUMAN_SCRIPTED with HUMAN_AICOMBAT and HUMAN_AISENSES; part 36's harness with
  `scripted_controller.cpp` and `checkpoint.cpp` linked), `scripted_test.inc`, run_suites `human.scripted`.
  - The derived AI controllers' common harness moved out of part 38's test into `derived_ai.inc` (the private members
    through explicit instantiation, the fakes, the events pair now with the pending events and the nearby actors, the
    control arrays, the walking points, the per-side vtables, the call wrapper, the pool priming, the original's
    controller made from a vtable); `sniper_test.inc` is built on it (the same checks).
  - The pair: the original's AI constructor's object with a copy of the scripted vtable (0x00811500, 0x1F0 bytes); its
    26 fields by `_re/tools/layout_offsets.py`'s table (at other port offsets), converted and compared around each call
    (the looped animation's name as a string: the port pads it with zeros).
  - Faked on both sides: the script's OnAlarm, OnAlarmDone, OnActionDone (three copies) and Advance, the formation's
    GetSlotTarget (a formation block per side, the leader at +0x74), UpdateDriving, the checkpoints' Find, the human's
    Freeze, PlayAnimation, SetCarryMode, SetScripted, Start / StopListening, GetHandsWeapon, RequestMoveItem, the body's
    GetActivityAnimLength, GetLeadPoint, timeGetTime (the port's import slot, the original's 0x0080f3f8), the scene's
    FindFrame (slot 0x7C, both sides' frames by name); the events' GetNearbyActor is a copy of the port's (the
    original runs its own).
- **The tests:** 24,000 calls: Attach, Detach; the handlers OnEnemySeen, OnBodySeen, OnBodyHeard, OnFriendHeard,
  OnLoudSound, OnShotAt, OnHit, OnSoundHeard (each chapter); the alarm's raise, SetAlarmType, GetAlarmType, SetAlarm,
  MoveToAlarm, StartScript, ResetAI (each role); MoveTo and AttackMoveTo (to the point the human stands at, or
  another), SetWaitForPlayer, FindCheckpoint, SetFormation, FollowFormation (each distance band); the eight move
  modes (seated or not), Sit, ActivityTurk, Disable / EnableEvents, SetMode, HasAIMode, StopAction, Suspend,
  SetOptimized; PickObject, DropObject, TakeWeapon, HolsterWeapon, Reload, PlayActivity (at a frame or not),
  SeesEnemy, SeesFriend, SetAnimation (network games' messages), AdvanceScript, PlaySpeech, LookAround1; the attack's
  Start, Stop and Clear; SaveGame (files byte for byte), LoadGame; Unknown104, Unknown114.
- **Results:** 76,517 checks, 0 failures, 22 s; lines: 478 of 478 in part 39a's ranges (the update, the firing, the
  attack's decisions, the meleeTrackers' alerts: part 39b).
- **Port fixes:** none. Found and noted in the source: when the human leads its formation, the original's
  FollowFormation returns the low byte of the human's address (AL left over: mostly true), so a leader's finished route
  does not tell the script; the port returns false and tells it (its result is not compared then). The original's
  HolsterWeapon asks the item's behaviour and drops the answer (the port does not: the getter is silent here).
- **Not tested:** the original's crashes the port fixes: OnLoudSound with a source that is not a human or none (the
  original asks it, as a human, whether the human is its friend); sounds without a source (the AI base turns toward it,
  both sides); a looped animation name of 56 characters or more (the original copies it unbounded).
- **Mutations** (`human/mutations/mutate_scripted1.py`): 366 mutants (280 generated, 86 for repeated lines); 17 not
  building; 324 of 349 killed; the survivors are equivalent (10), ties at FollowFormation's distances (5), the clients'
  animation message name (5, not compared) and weak (5: a narrow distance band, saved files that always hold every
  chunk).
- **Harness lessons (my mistakes):** a function and a member tag of the same name; `->*` and a subscript again; the
  scene had no FindFrame (a call through null); the formation block shared by both sides (the original's leader word
  overwrote the port's); the original's SetFormation given the port's formation; sounds without a source; the friend's
  enemies from a human whose lists are not converted; the tested human at the origin (a difference and a sum look
  alike: the mutants of FollowFormation's distance lived).

## Human part 39b (the scripted controller: update, tick, seat weapon, firing, attack, alerts, neutral, `scripted_controller.cpp`, 2026-11-16)

- **The build:** part 39a's (`human/build_scripted.bat`, `scripted_test.inc`).
  - The vehicle: part 35's car (a car, a static weapon or a submarine) with its seat weapon's rounds and origin added
    (the port's slots, the original's 0x9C and 0xA8 with its hidden return pointer); the submarine's SetControls is
    part 23's fake. The neutral controller: its own pair (vtable 0x008113e8, 0x140 bytes), the AI pair for its calls.
  - GetLeadPoint is a copy of the port's (enemy_gunner.cpp's; inline in the original). The heard sounds (the original's
    +0x2A8) emptied before the alerts and compared after, by their contents (each side's pool objects). UpdateMove's
    fake ends the route now and then (the move order's FLAG_ROUTE, +0x50, on both sides). Part 23's weapon Fire fake
    logs the flags' low byte (the weapons read only that).
- **The tests:** 12,000 calls: Update, Tick (in a vehicle or not, the formation, waiting for the player, the route's
  end, the attack, the firing time, network games' send mask), ControlVehicle, TurnAt, TurnSeatWeapon, AttackActor,
  AttackFrame, UpdateFireTime (from a seat or not), GetSeatAimDir (scattered or not), AimAndFireSeatWeapon (a gun or a
  cannon, no rounds), DecideAttack (each weapon behaviour, targets up to 330 m), UpdateAttack, ProcessMeleeTrackerAlerts
  (the nearby humans and others), the neutral controller's Attach, Detach and Unknown114.
- **Results:** 113,787 checks for the file (parts 39a-b), 0 failures, 29 s; lines: 791 of 791.
- **Port fixes:** ProcessMeleeTrackerAlerts looks a nearby actor up a second time for its controller, as the original
  (it looked it up once: the same answer, one call fewer).
- **Not tested:** the original's crashes: a meleeTracker alert whose source is gone and sounds without a source (the AI
  base turns toward the source unchecked, both sides), a nearby actor that is gone (the original reads it: the port
  checks), firing from a seat outside a vehicle and a firing with no target and no frame (both read them unchecked), a
  weapon without its model (both); the same event twice in the pending queue (impossible in play: the original
  releases it twice and leaves one).
- **Mutations** (`human/mutations/mutate_scripted2.py`): 310 mutants (248 generated, 62 for repeated lines); 21 not
  building; 256 of 289 killed; the survivors are not compared (5: stale w words, the silent release), equivalent (8),
  ties (9) and weak (11: waiting for the player, whose fake is the human itself; DecideAttack's range bands).
- **Harness lessons (my mistakes):** a second fake of the submarine's SetControls; sounds and alerts without a source;
  the same event twice in the pending queue (the original then leaves one: I took it for a port difference until a
  run without duplicates agreed); firing from a seat outside a vehicle, a firing with neither target nor frame, a
  weapon without its model (crashes of both sides); a nearby actor that is gone; the neutral's random state without
  the walking network; 20,000 calls made the test 41 s (12,000: 29 s).

## Human part 40a (the player controller: the life cycle, the update and the tick, the keys and the mouse, the breath, `player_controller.cpp`, 2026-11-16)

- **The build:** `human/build_playerctl.bat` (`playerctl_test.inc`; part 33a's AI controller build with
  HUMAN_PLAYERCTL, the movement's OnFrozen wrapped and logged).
  - The pair: the port's C_PlayerController (ExecuteCommand logged); the original's made by the base constructor
    (0x00582ff0) with a copy of its vtable (0x00811848), 0x560 bytes, its own fields cleared from 0xC0. Its fields
    from layout_offsets.py; the pending command without its vtable word; the human's net object kept per side as part
    33a's AiCall.
  - Fakes logged alike on both sides: the camera's slots, the car's SetInput, the enemy records' Remove, the
    first-person view's CanUseWeapon / SetAimView / SetWeaponView, the profile's detail settings (invertMouse at
    +0x39, a block per side), the sound direction, AddScore, the replication's epochs, the game's VIP slot, objective
    status and binoculars view, the heavy fire support and the squad commander (fake objects, per-side vtables),
    RequestThrowItem, the team's IsEnemyKnown and ReleaseHoldingMembers, the player's members in other files
    (FlushMessages, TickLocal, UpdateRemote, WeaponControls with the four latched buttons), the seats' flags, camera
    and occupant (part 23's actor vtables).
  - Compared besides the fields: the shared controls (axes and buttons: Tick latches into them), the human's frame's
    on flag and SetOn count (only bit 0: reading the matrix sets others on the port's side), the players unlocked.
  - The state: part 23's controller and part 33a's AI state; a network role, the local player's lives (the team's and
    the player's, co-op or not), the pace and stamina, the stance, part 23's car (armed seat or not) and climbing now
    and then, the spectator's camera, the frame turned (now and then tilted), the weapons' ready times, the mission's
    objectives (a VIP's), the squad member taken over (part 23's second controller as a squad member).
- **The tests:** 12,000 calls: Attach (no previous, a base controller, a squad member), Detach, OnDeath, OnKilled,
  Update (spectating, detached, the timers at their edges), Tick (the latches), HandleInput (each control random;
  the pace keys one at a time; the mouse far, or exactly one step; a scripted view or an interaction; the vehicle
  seats; marching against the endurance; dropping the hands weapon), HoldBreath.
- **Results:** 27,023 checks, 0 failures, 7 s; lines: 330 of 332 (the two `> 2 pi` wraps after atan2, which cannot
  exceed pi).
- **Port fixes:** Attach clears the whispering flag (the original's +0xC0; a squad member's taken over sets it
  again); HandleInput asks the profile for invertMouse again around the seat weapon's controls, as the original;
  the turn's limit asks the seat before the climbing state, both always, as the original.
- **Not tested:** the original's crashes the port checks: holding the breath through the scope with no hands item,
  the ready time of a hands item that is not a weapon, dropping through a missing hands container; a squad-type
  previous controller that is not a squad controller (the original reads past it; impossible in play); a VIP game
  without a mission (both read it unchecked).
- **Mutations** (`human/mutations/mutate_playerctl1.py`): 379 mutants (327 generated, 52 for repeated lines); 19 not
  building; 323 of 360 killed; the survivors are equivalent (24: unit axes normalised again, periodic angle wraps,
  the second pitch clamp, stores made anyway), ties (10) and not reached here (3: the spectated slot 0, the index -1
  before the controls' arrays).
- **Harness lessons (my mistakes):** the build script at first still linked remote_controller.cpp (a sed pattern's
  backslash); a second OnDamaged; SetUpPlayerFakes not called; the action command's actor and member are pointers; a
  squad-type previous that was a base controller; no mission in the VIP path; a binoculars message freed with
  _aligned_free; the control arrays unmapped; holding the breath without a hands item (the original's crash); a
  null LockPlayers; a heredoc collapsed a macro's backslash; the mutation runner without the OnFrozen wrap (failing
  baseline); the pace and mouse keys written inside the controls' loop; the shared buttons, the frame's on flag and
  the unlocks not compared at first (72 survivors, 37 after); I first read the original's frame flag after DiffCall
  had put the port's state back, and once took an already-killed mutant for a flaky one.

## Human part 40b (the player controller: squad commands, the radio's target, comments, sight, sounds, aim, `player_controller.cpp`, 2026-11-16)

- **The build:** part 40a's (`human/build_playerctl.bat`, `playerctl_test.inc`) with the collision world's RayTest
  wrapped (the original's thunk 0x00718830 redirected): DesignateTarget's ray answered and logged from the test's
  state (hit or not, where). Part 23's GetActor (the players: none, the tested human, the other), IsSpectating and
  IsInPlay fakes on both sides; the heavy fire support's requests logged whole (target, way, kind, planes, height,
  caller, team).
  - The state: part 40a's; the mission a multiplayer one with its two aircraft kinds (the original's +0x50 / +0x54);
    the radios found or not; the players in the first 32 slots; the other human up to 45 m away (its frame, a body
    frame of its own, its position and health); the speech (part 23's: a set or none, the delay, the code, the
    sample's length); the stance and pace; the shooting skill; the tested human's body turned and now and then
    tilted for the sight.
- **The tests:** 12,000 calls: SetWhisperingFor, ExecutePendingCommand, OnSquadCommand (each role; commands 0..0x3D,
  the radios', SQUAD_COMMAND_ON_ACTOR and its quiet one with their sentences), PlayCommandGesture, DesignateTarget
  (artillery, air attack, others; a ray hit or not; a radio or not), SayNear, OnEnemySeen, OnEnemyLost,
  OnKilledEnemy, TestSightOfHuman, OnSoundHeard (each chapter), GetAimBonus.
- **Results:** 51,024 checks for the build (parts 40a-b), 0 failures, 10 s; lines: 185 of 185 for part 40b's ranges.
- **Port fixes:** none needed in this half.
- **Not tested:** the port's fixes the original does not share: squad commands past 0x3D (the original reads past
  its speech tables), the artillery kind on a single player mission (the original reads past the mission: the port
  has 0), a non-human actor in the first 32 slots for SayNear (the original takes it for a human; the harness's
  slots hold only humans); the real collision ray (faked).
- **Mutations** (`human/mutations/mutate_playerctl2.py`): 211 mutants (197 generated, 14 for repeated lines); 20 not
  building; 174 of 191 killed; the survivors are the fixes not reached (3), equivalent (5), ties (6) and weak (3:
  the sight triangle's edges against 1 m^2).
- **Harness lessons (my mistakes):** the first run's 27 survivors: the human had no speech set in this build, so Say
  returned before logging any sentence (the comments' and the commands' sentences unchecked), and the body frame was
  never tilted; a helper's name (FloatBits, not part 33's FloatBitsOf).

## Human part 40c (the player controller: hits, the view's kicks, saved games, the view modes, the medkit, `player_controller.cpp`, 2026-11-16)

- **The build:** part 40b's (`human/build_playerctl.bat`, `playerctl_test.inc`).
  - Part 23's logging fake of the original's OnDamaged taken back (RestoreOriginal): tested here. RequestAction
    (player_weapons.cpp's: the base's LoadGame raises or lowers the weapon) logged on both sides.
  - The worn definitions get a vtable per side while a call runs (the port's GetCarryMode slot is the original's
    GetBehaviour slot in part 23's shared one); the carry mode a fake on both. The hands weapon's scope: the
    definition's secondary component (the port's member, the original's +0x4C), its aimed field of view at the
    port's offset (found by GetAimedFovFactor) and the original's +0x10.
  - The copy protection tables the original checks in UseMedkit and SetViewMode (0x008ca840 / 0x008caf40 by the
    words at 0x008cac66 / 0x008cb366 and 0x008cac92 / 0x008cb392) made equal, as in a game that passes; the port
    dropped the checks.
  - The binoculars message's on and fromClient flags logged with the queued net messages (part 33a's logger): on at
    +0x30 in the original and an MSVC build, at +0x2D in the g++ build (the base's tail padding).
  - The original's pending command keeps its vtable word (0x00810528, as its constructor leaves it): saves write it.
  - The state: part 40b's; the helmet or none; the hands weapon's carry mode; the local player and the first-person
    view; the difficulty 1..4; the binoculars on the belt or none, their frame; the scope's field of view; part
    23's healing state (the patients by id, their health against the initial, the first aid skill, the medkit's
    charges, behaviour and uid).
- **The tests:** 12,000 calls: OnHit (the helmet's and the weapon's zones more often; an attacker none, the other,
  the human itself, another kind of actor in network games), OnDamaged, SaveGame and LoadGame (about 1,200 rounds:
  each save byte for byte, the port's file loaded on both sides, now and then a hand-written state chunk of 0..22
  bytes, a command chunk, an unknown chunk), SetViewMode (-1..4 from any mode), UseMedkit (a patient or none, a uid
  or -1).
- **Results:** 78,671 checks for the build (parts 40a-c), 0 failures, 20 s; lines: 172 of 172 for part 40c's ranges,
  701 of 703 for the file (the two wraps past two pi after atan2).
- **Port fixes:** none needed in this part.
- **Not tested:** the port's fixes: ScopeFovOf without a hands item or scope (the original reads them unchecked), a
  missing head container, another kind of attacker in a local game (the original takes it for a human: a crash in
  the harness); the pending command's actor and member in a save (both write the raw pointers: each side's own
  address; only null tested), the original's command padding (+0x34..+0x3F: it saves its object's stale bytes, the
  port zeros; cleared before each save); a failed copy protection check (dropped by the port).
- **A weakness the original shares:** a save made while a squad command waits (about a second after it is given)
  stores the command's actor and member as raw pointers, which a load restores as they were: stale pointers in the
  new session. Not changed on main.
- **Mutations** (`human/mutations/mutate_playerctl3.py`): 195 mutants (185 generated, 10 for repeated lines); 13 not
  building; 176 of 182 killed; the survivors are not compared (2: the hit message's w words), the port's fix not
  reached (1) and equivalent (3).
- **Harness lessons (my mistakes):** the original's OnDamaged was still part 23's logging fake; a non-human
  attacker crashed the original's IsHostileTo in local games; RequestAction was an unexpected call; part 40b's found
  item (the gun) stood in for the medkit; my carry mode fake in the shared definitions' vtable replaced the
  original's GetBehaviour; the original's command vtable word was never set, and my hand-written command chunks
  loaded random words and padding into it; 2,400 save rounds made the test 29 s; the binoculars flags were not
  logged, and at first I read the port's at the MSVC offset.

## Human part 41 (the player controller's weapons, `player_weapons.cpp`, 2026-11-16)

- **The build:** `human/build_playerwpn.bat` (`playerwpn_test.inc`, HUMAN_PLAYERWPN): part 40's player build with
  player_weapons.cpp linked (part 40's logging fakes of WeaponControls taken out) and part 33's senses and fighting
  harness (HUMAN_AISENSES, HUMAN_AICOMBAT: its sources and fakes) alongside; its tests alone.
  - Faked on both sides and logged: the body's GetEquipmentFlag1, RemoveCollision and RestoreCollision, the camera's
    GetCameraPos (slot 0x64), the human's GetFacingDir and IssueCommand, the player's IssueCommand (player_net.cpp's),
    the weapons' PlayEmptySound, PlayShotSound and GetMaxAmount (slot 0x10), the hands container's SetLockTime (slot
    0x4C), the cheats (0x00720f50: part 23's fake), the collision world's RayTestDynamic (wrapped; the original's
    0x006f7cc0). The static ray test is part 40b's.
  - Per side while a call runs (part 40c's hook): the weapon component's vtable (its GetAimedDispersion at the port's
    slot and the original's 0x10), the hit volume's actor (+0x48: a human, the other, part 23's actors), the car
    charge's frame data (its actor).
  - The copy protection checks (dropped by the port) made to pass: the one that drops the weapon now and then
    (0x008cacda / 0x008cb3da) and the spread's (0x008cad08 / 0x008cb408; it runs only past mission 8, and the
    harness's game is at 0, so it never ran).
  - After each call the weapons are compared too: the attack time, the flags and the throw's strength (the original's
    +0x58, +0x60, +0x98).
  - The state: part 40c's; the buttons and axes (now and then none); the hands weapon (the gun, the grenade, none, a
    stale one) and its behaviour (each the controls know, others); its amounts, reload, fire answer; the charge (its
    time near the caps, its action); the throw delay; lying now and then; the spawn protection and its end; the
    seat; the camera's position and the rays' answers (hits about 3 m from the muzzle); the cheat; the aim view; the
    component's dispersion and attack delay; the scope (its kind, aim mode, or none).
- **The tests:** 24,000 calls: WeaponControls, MeleeStrike, UpdateCharge (part 41a); ApplySpread and ApplyRecoil
  (their directions compared word for word), UpdateZoom, UpdateWeaponReady (part 41b).
- **Results:** 72,006 checks, 0 failures, 9 s; lines: 367 of 367.
- **Port fixes:**
  - ApplyRecoil asks the definition's behaviour again for behaviour 4 before the scope, as the original.
  - C_FirstPersonView's layout: two unused words at +0x50 and +0x5C were missing, so m_itemType, m_on, m_binoculars,
    m_aimView and m_pose sat 4 or 8 bytes before their documented offsets (harmless inside the port; found when the
    harness's shared first-person view gave the aim view's byte at +0x5A to the original only). Static asserts now
    hold the offsets up to the fading vector (past it only MSVC6's 16-byte vector keeps the original's).
- **Not tested:** the port's fixes: MeleeStrike with no hands item, UpdateZoom with no buttons (the original reads
  them), the belt throw's and the hands' grenade lock through a missing hands container, a weapon's fields of another
  item kind; UpdateCharge and the spread/recoil without controls or a hands weapon (their callers check); the copy
  protection checks (dropped); a launcher whose firing empties the hands (the fakes never change them).
- **Mutations:** `human/mutations/mutate_playerwpn1.py` (part 41a): 245 mutants (220 generated, 25 for repeated
  lines); 22 not building; 214 of 223 killed; the survivors equivalent (4) and ties (5).
  `human/mutations/mutate_playerwpn2.py` (part 41b): 164 mutants (135, 29); 11 not building; 151 of 153 killed; the
  survivors equivalent (2: BuildBasis's y squared, always 0).
- **Harness lessons (my mistakes):** the dispatcher ran part 33's combat test first (its branch came earlier); the
  original crashed reading a missing hands item (MeleeStrike) and missing buttons (UpdateZoom), the port on null
  axes in a direct UpdateCharge (its caller checks); the weapon component had no vtable in this build; a null item
  frame (real weapons have a "shdum" model); the cheat fake not redirected; the original asks GetBehaviour twice
  (the port fixed); the aim view byte (the layout fix above); the first mutation run's 29 survivors (weapons not
  compared, never lying, the edges); heredocs collapsed my "\n" three times (fixed with Edit).

## Human part 42 (the player controller's network side, `player_net.cpp`, 2026-11-16)

- **The build:** `human/build_playernet.bat` (`playernet_test.inc`, HUMAN_PLAYERNET): part 41's with player_net.cpp
  linked (parts 33 and 40's stubs of its members, its callbacks and its messages' vtables taken out; the net messages'
  bases' key functions defined, so that their vtables are emitted); its tests alone.
  - Part 34's remote controller fakes copied (the client's item moves, the movement's OnRemoteAction, the visual's
    SetDirection, the human store's and the net game's stats and rank, the net object's queue of net messages and
    the property's tracks), the movement's UseObject, part 23's car ApplyControls and submarine SetControls (set only
    in its own test), timeGetTime (the port's import slot, the original's 0x0080f3f8). The replication's local
    object (+0x30 of part 22's and part 33's blocks) gets a logging vtable.
  - The net messages the test hands out are kept by both QueueMessage fakes (ProcessNetMessages queues them again).
  - The weapon component gets GetReloadTimeMs (the port's slot, the original's 0x20); the weapons' shot interval is
    set (the port's offset, the original's +0x68).
  - Compared besides part 40's: the messages queued for the server by their fields (part 33b's comparison; the
    original's take-from-box and transfer messages carry a pick-up's and a move's type word, taken from their classes
    first), the action's and the damage request's fields directly, the dive's fromClient flag; the shared controls
    in every player build (they were shared only by the fighting's setup, which parts 41 and 42 never ran).
  - The streams: the sender's quantization is the test's (+0x2C in the original and an MSVC build, +0x28 in this g++
    build: m_unreliable sits in the base's tail padding).
- **The tests:** 60,000 calls: UpdateRemote, TickLocal, ProcessNetMessages, ServerAction (part 42a); the action's,
  the dive's and the binoculars' Write and Read, NetCreateMessage, NetReadControlBits, NetWriteControlBits; the
  requests and FlushMessages (part 42b); OnMessage, OnMessage2, HandleMessage, HandleMessage2 (part 42c, each
  handler's known types; the server's now and then a type it skips).
- **Results:** 154,818 checks, 0 failures, 19 s; lines: 624 of 626 (the inventory check's Read on a client, left out:
  the original leaves its message's human unset).
- **Port fixes:** ApplySoldierClass asks the net game for the player's stats again before the profile's skills, as the
  original (it read the class from the first answer).
- **Not tested:** the port's fixes: the scope's view back without a hands weapon (TickLocal; the original reads it
  unchecked: the test gives one), squad commands past the speech tables in a client's action message, the control
  state message the original leaks, the messages it frees before reading their containers, the inventory check's stale
  human; a net message whose reference count reaches 0 (ReleaseNetMessage's delete).
- **Mutations:** `mutate_playernet1.py` (42a): 237 mutants; 15 not building; 204 of 222 killed (the survivors
  equivalent 13, a tie, uncompared w words 2, a port fix not reached, a leak). `mutate_playernet2.py` (42b): 105; 10;
  93 of 95 (a w word, an equivalent store). `mutate_playernet3.py` (42c): 177; 21; 143 of 156 (the inventory check
  not reached 2, leaks 9, equivalent 2).
- **Harness lessons (my mistakes):** stubs of player_net.cpp's members, callbacks and vtables clashed in three files
  (gated); the test's net messages freed by the QueueMessage fakes; the replication's local object without a vtable
  (part 22's block is the port's, part 33's the original's); the original's car and submarine controls were real; the
  original crashed with no report inside TickLocal (its exception frame's MSVC6 handler cannot run here: a
  first-chance handler found the scope read of a missing hands item); GetReloadTimeMs unfaked; the original lambda's
  argument is DiffCall's movement, not the controller; the stream's sender offset (g++ tail padding); the inventory
  check's stale human and squad commands past the tables in the input; the damage request's fields read only in
  part; the first mutation summaries for 42a read one line of joined output (1 survivor instead of 35) before I
  recounted.

## Human part 43 (the enemy AI, `enemy_controller.cpp`, `enemy_tasks.cpp`, `enemy_combat.cpp`, `enemy_gunner.cpp`, 2026-11-17)

- **The build:** `human/build_enemy.bat` (`enemy_test.inc`, HUMAN_ENEMY): part 36's fighting build with the enemy's four
  files and checkpoint.cpp linked (derived_ai.inc's copy of CoverAgainst left out); HUMAN_ENEMY_PART picks the parts (a,
  b, c, d) to run. The original's controller: the AI constructor's object with a copy of the enemy's vtable (0x00811730,
  0x1E0 bytes); its fields +0x140 .. +0x1E0 converted and compared around each call (the MSVC6 vectors of points and
  guns; the search's event and the guard's gun share +0x148 in the original: the one set converted, an event the call
  allocated compared by its fields).
  - Faked on both sides and logged: the walking network's GetActor, FindActorPoint, FindNearestPoint, GetNeighbourCover,
    GetReachable, GetReachableCopy, GetReachableCover (appending, as the flood fill does) and PickCoverPoint (dropping
    the candidates the test names, each side's vector); the human's FindItemByRecordType, RequestMoveItem,
    RequestTransferItem; the events' GetAttacker, GetStrongestStimulus, IsAwayFromNearbyActors; the body's GetSector,
    GetUnknown6E0, RemoveCollision, RestoreCollision; RayTest and RayTestDynamic; the seat weapons' rounds, origin and
    occupant (the seat car and part 23's actors as the post's guns); the car's and the submarine's controls. The events'
    PopFront and GetNearbyActor and an event's SaveGame / LoadGame are the port's copies (the original runs its own).
  - Shared harness changes: the original's ReleaseHumanEvent fake is __stdcall (RET 4: parts 38-39 never reached the
    call); the AI lists' hook also converts the seen bodies (+0x284); the actors module's GetActor logged once per id in
    a call (the original repeats the inline lookup); the checkpoints' Get not logged in this build (the original makes
    lookups it drops); the definitions' behaviour per definition (g_behaviourOf); the 16-bit points the original pushes
    compared as 16 bits; HUMAN_DUMP_CALLS (both sides' calls of the first differences, =2 with their numbers) and
    HUMAN_FIRST_CHANCE (a first-chance access violation handler) in every human build.
- **The tests:** 120,000 calls (30,000 a part): 43a the life cycle, events, the task's choice, weapons, firing, the
  grenade, the line of fire, saved games (an older layout's chunks too); 43b the guard, the search, morale broken; 43c
  the combat task, the weapon's range, stances, the points to fight from; 43d the gunner, the seat weapon's targets and
  aim, the post's guns.
- **Results:** 365,087 checks, 0 failures, 84 s (about 21 s a part; a part alone in its mutation runs); lines: 2,502 of
  2,508 (the destructor: the port's fix releases a search's event the original leaves; the gunner's aim at a lying
  target's position when rand() gives 0).
- **Port fixes:** Tick in a vehicle that is not a car, a submarine, a static weapon or a Junkers walks and fires (the
  port did nothing); the search's walk raises the weapon after sneaking crouched too; PlanGrenade's farthest point
  summed z first (two groups as far from their middle tie and the rounding picks); S_Checkpoint::GetDirectionBit
  compares with the original's double constants (a direction at a sector's edge); GunnerEvent asks for the vehicle
  before it looks the shooter up (the original's order); human.h's ReleaseHumanEvent is __stdcall.
- **Not tested:** the original's flaws the port shares, steered clear of: UpdateFiring reading the task StartGuard
  leaves out (no home point); the combat's move and approach and the launcher's state measuring to a target UpdateTimers
  forgot; PickGunnerTarget taking a stimulus's non-human source for a human (GetLeadPoint reads its controller);
  network games' IsFriendOf taking any actor for a human. The port's fixes: FindApproachPoint's list run below empty
  (the target kept 24 m away where the call can be reached, or the removals counted), CanFireMoving with no hands item,
  StartSearch's missing body, the guard's missing gun, IsOutOfGunReach's missing vehicle, PickGunnerTarget's division by
  a time left of 0, GunnerEvent's non-human killer and source, the destructor's event.
- **Mutations:** `mutate_enemy1.py` (43a): 709 mutants; 52 not building; 616 of 657 killed (94 %). `mutate_enemy2.py`
  (43b): 636; 17; 544 of 619 (88 %). `mutate_enemy3.py` (43c): 518; 13; 438 of 505 (87 %). `mutate_enemy4.py` (43d):
  370; 32; 296 of 338 (88 %). The survivors: equivalent, ties, silent event releases, sums with heights 2 m apart, and
  weakly tested state combinations (each file's docstring).
- **Harness lessons (my mistakes):** the shared ReleaseHumanEvent fake's calling convention; the hands container left
  to part 23's per-side swap; worn items with no vtable as hands items; the fakes answering -1 where the network answers
  0xFFFF; part 23's second controller without a human; the other humans' velocities and containers not set on the
  original's side; tasks released into one side's pool only; the test's off-network points where the code does not
  check; the human among its own enemies in view (every distance threshold passed); everyone at height 0; the network
  fakes replacing the lists their callers cleared; a height offset that accumulated; the mutation runs' first copy taken
  from an older harness (its baseline failed in quick mode: a part must pass in quick mode too); a background chain
  run from a script the edit tool had not rewritten (stopped); heredocs collapsing "\n" again.

## Human part 44 (the squad AI, `squad_controller.cpp`, `squad_order.cpp`, `squad_actions.cpp`, `squad_combat.cpp`, `squad_movement.cpp`, 2026-11-17)

- **The build:** `human/build_squad.bat` (`squad_test.inc`, HUMAN_SQUAD): part 36's fighting build with the squad's five
  files and checkpoint.cpp linked; HUMAN_SQUAD_PART picks the parts (a, b, c, d) to run, HUMAN_SQUAD_CASE one case of
  a part alone (2,000 rounds). The original's controller: the AI constructor's object with a copy of the squad's vtable
  (0x008109f8, 83 slots, 0x260 bytes); its fields +0x140 .. +0x260 converted and compared around each call (the order
  queue an MSVC6 vector; engagePos, holdDir and the action's pos copied whole and compared without their w, which
  LoadGame and several writes leave a stack word in the original). The orders: each side's own (the port's pool, the
  original's AllocHumanOrder 0x005825c0), made alike, paired and compared by their fields (the path's ends, points and
  capacity, the marker's place, data and parent, the direction by x, y, z).
  - Faked on both sides and logged: the squad commander (the port's object with a fake vtable, the original's global
    0x0089ad40); the player team's selected member, members, the enemies it knows and sees (the records' Add and Remove);
    the formation's slots; the car's seat entry, input, crew side and entry; the seat car's seats (count, occupant,
    camera, the gun's rounds and origin) and part 23's actors' seat count and occupant; the human's last name, medkit,
    controller type, item counts and collection, FindItem (each side's own weapon), requests and commands; the walking
    network's path length, FindPath (each side's vector), cover, names, actors and points (16-bit points compared as 16
    bits); the wire's cut; the items module's room; the air attacks, the Junkers and the submarines; the temporary
    actors' mines and items (an item actor with a weapon); the command panel's order markers (a pool of 16 frames per
    side, the first free one); the map path's points (a point per walking point and the end); the body's sector and
    spheres; the collision world's ray tests; the orders' boxes in the collision world (logged instead of added: their
    fields at AddVolume, flags and owners at MoveVolume and RemoveVolume); the original's C_fstr constructor (a real
    C_str buffer); the leader's controller (ctl2 with a faked last target).
  - Shared harness changes: WordsNear takes a NaN and the same NaN signed the other way as equal (g++ folds x * -1.0f
    into a sign flip, x87's fmul keeps the sign); g_fireDirW (off in this build: the fire order's direction w and the
    move's pos.w left out); g_findPathFake and g_volumeLogger hooks; LogPrint drops the squad's weapon switch's second
    %s (the original passes none); CT_Unexpected prints the caller's address; HUMAN_DUMP_CALLS also dumps AiCall's
    failing calls (both sides, =2 with numbers); the fire order's comparison prints the words.
- **The tests:** 44a 30,000 calls on the life cycle, updates, the idle member, the order queue, icons, senses, vehicles,
  saved games, plus 12,000 on the orders' own functions (markers, actor points, paths, changes, Init again); 44b 30,000
  on the commander's commands, the orders started, the action executed (to its command, a bound's hop, the walk's
  start), follow and hold, the combat orders, looting and equipping; 44c 30,000 on events, stimuli, hits, targets,
  bursts, the line of fire, weapons, the gunner, cover; 44d 20,000 on the pace, stepping aside, room for the leader,
  attack points, special points, a grenade thrown.
- **Results:** 382,038 checks, 0 failures, 68 s (quick mode: a 10 s, b 5 s, c 5 s, d 4 s); lines: controller 826 of 827,
  orders 172 of 185, actions 1,066 of 1,093, combat 972 of 1,032, movement 172 of 179 (3,208 of 3,316).
- **Port fixes:** five inline normalizes summed in the original's order ((y² + z²) + x², NormalizedGame): UpdateCombat's
  and UpdateGunner's engage direction, VehicleCombat's aim, MakeRoomForLeader's step, both of FindCoverPoint's; the
  gun's cone test summed (y·y' + z·z') + x·x'; FinishAction asks for the leader's stance again for each test (as the
  original; no change in behaviour).
- **Not covered:** the melee trackers (no tracker actor in the harness: FindNearMeleeTracker's list, the trackers as
  targets), the air attack actors (GetVehicleAimDir's lead), supply boxes; the path's growth past its first 8 points (the
  order's paths hold two), the order's destructor (the pool's), Init with a marker already on, QueueWatchOrder's null
  model (every caller passes one), DropAction's type 0x200 (no action has it); SetCombatOrder's MODE0_ALT with empty
  hands (the original's crash); a few rare answer combinations (the swap's helmet refused, the line blocked, the far
  leader's pace).
- **Original flaws, steered clear of (the port shares them):** FindEnemyTargeting takes an enemy's last target for a
  human (a vehicle would be read as one); ReactToStimuli takes a stimulus's source for a human; CanFireAt and
  HasClearLine read the fire target's body frame (the squad only sets humans there). The port's fixes, avoided by the
  tests: SetCombatOrder's MODE0_ALT with no hands item, ProcessOrders removing from the empty queue, UpdatePath's and
  GetActorPoint's empty paths, QueueWatchOrder with an empty queue.
- **Mutations:** `mutate_squad1.py` (controller, 44a): 857 mutants; 43 not building; 719 of 814 killed (88 %).
  `mutate_squad2.py` (orders, 44a): 212; 15; 156 of 197 (79 %). `mutate_squad3.py` (actions, 44b): 1,166; 87; 923 of
  1,079 (86 %). `mutate_squad4.py` (combat, 44c): 1,146; 120; 784 of 1,026 (76 %). `mutate_squad5.py` (movement,
  44d): 196; 18; 140 of 178 (79 %). The survivors (rerun with all four parts on the final harness): equivalent,
  ties, code this harness cannot reach (melee trackers, air attacks, the order path's growth), values not compared
  (the order box's centre, stale in the original), and weakly tested combinations (each file's docstring). The
  combat file's 76 % is the weakest of the human parts so far: its distance thresholds and the vehicle attack's walk
  are rarely met by the random state.
- **Harness lessons (my mistakes):** the records' Remove redirected at the camera's address; the team's g++ layout
  differing from the original's (the records' pointer logged); a vehicle's unfaked seat slots; the original's GetSlots
  answering an array of vector pointers; the containers' vtable-less worn items in the belt and the long gun; the hands
  item and container out of step (WeaponsToOriginal); the human's network role random (the squad is single player's);
  the other humans sharing the AI pair's controller (its fire target became an enemy's last target); attack and follow
  actions without their targets; a car actor without its "body" frame; the original's cover compass never initialised
  (its static initialiser); pooled tasks' stale words; the markers' pool handing one frame to every order; Git Bash's
  sed -i turning CRLF files LF-only (a mutation list made from one had to be redone); heredocs collapsing "\n" again.

## Human part 45 (the squad commander, `squad_commander.cpp`, 2026-11-17)

- **The build:** `human/build_commander.bat` (`commander_test.inc`, HUMAN_COMMANDER with part 44's HUMAN_SQUAD build):
  squad_commander.cpp linked, the squad controller's 46 methods it calls faked and logged on both sides
  (`commander_fakes.inc`, made by `gen_commander_fakes.py`: the port's by `--wrap`, the original's redirected; the wrap
  flags are the bat's CMWRAP). HUMAN_COMMANDER_CASE runs one case alone (2,000 rounds).
  - The commander: the port's g_squadCommander against the original's object 0x0089ad40 (its vtable 0x0080fb28) and
    idle reports 0x0089ad30; its fields converted and compared around each call (the five groups and the manoeuvre's
    members as MSVC6 vectors of mapped humans; the door's and ladder's places and the formation scale without their
    w, stack words in the original). The port's idle reports (an unnamed namespace's vector) are found once in the
    image's writable sections by their contents.
  - The members: the three humans, each with a squad controller pair of its own (part 44's for the tested human, two
    more made alike, their humans set: the original's ApplyOrder asks the controller's human for its leader); each
    pair's fields compared as part 44 compares its own; the two others' orders made once (the commander does not change
    the queues); CancelOrders logged on both sides.
  - Faked and logged: the formation's SetType and SetScale; the human's GetOrderCount and CancelOrders copied
    (human_control.cpp is not linked); GetControllerType answered per human, silently (the port's fixes ask where the
    original does not): the squad's, the selected soldier's a player's now and then, with a block of a player
    controller's size per side (the original writes its squad combat order at +0x548, past a squad controller).
  - The original's static initialisers called: the cover slots 0x004595f0, the unused places 0x004596a0, MoveNear's
    places 0x0045b7d0, the checkpoints' eight directions 0x005244c0 (all zero before: every place the point itself).
  - Shared harness changes: the ray test's hit normal answered (g_sq.rayNormal: each side read its own stack before);
    the sight test's answer (derived_ai.inc g_daSight), the scene's sector by place (self_test.inc g_sectorAt); the
    task type's mismatch message gives both types; squad_test.inc's commander fakes left out of HUMAN_COMMANDER builds.
- **The tests:** 30,000 calls: Tick (its timers' and idle reports' edges), OnMemberIdle, CollectGroup, HasGroupMembers,
  IsInGroup, CollectFor, OnFriendlyFire, DelayFormationChecks, OnPlayerNoise, ReleaseFollowers, Reset, the getters and
  trivial slots, ExecuteActionCommand on every action (TakeCover and MoveNear with the squad within 25 m of the
  selected soldier and up to eight cover points), ExecuteCommand on the commands' own actors (doors, mines, explosives,
  cars, the static gun, item actors, supply boxes and ladders standing in, humans, the wire), the manoeuvres
  (OnOrderMoveDone in every stage); the doors' states and keys, the uniforms' sides, the third's own uniform.
- **Results:** 90,005 checks, 0 failures, 17 s (quick mode 7,500 rounds); lines 711 of 733.
- **Port fix:** HasSilencedWeapon looks the three silenced weapons up before checking them, as the original (the calls'
  order only).
- **Not covered:** TakeCover's empty slots filled from the spares and the nearest point (two members at most besides
  the selected soldier: the harness has three humans); the grenade room's second member and the door's third (the
  picks keep one and two: dead in the original too); the port's fixes (no ground point, no slot or member within
  100 m, a ladder without its two points).
- **Original flaws, steered clear of (the port fixes them):** TakeCover and MoveNear read the selected soldier through
  null; TakeCover orders the word before its member list when nobody is within 100 m of a cover (seen as a crash at
  0x0045a937), and reads the ground point through null; Reset keeps the last mission's groups and idle reports;
  IsInGroup reads past the groups for GROUP_ALL.
- **Mutations:** 710 mutants (630 T, 80 R); 67 not building; 540 of the 643 building killed (84 %), 103 survived: 38
  not reachable with three humans, the port's fixes, equivalent ones (the w words, the uniform test flipped on both
  sides, presets overwritten), ties and weak combinations (see mutate_commander.py).
- **Harness lessons (my mistakes):** the original's tables zero (static initialisers not run: four of them); the hit
  normal never answered; the original's getters called with an argument they do not take (the coverage build
  crashed); the member pairs without their humans; groups listing a member twice; the selected soldier as a player on
  a squad controller's 0x260 bytes (the original's +0x548 write landed in the harness's pointer map, which showed first as a
  corrupted task type: I blamed the members' orders before finding it); heredocs collapsing "\n" again (four times).

## Human part 46 (the AI controllers' movement, `ai_movement.cpp`, 2026-11-18)

- **The build:** `human/build_aimove.bat` (`aimove_test.inc`, HUMAN_AIMOVE with part 43's HUMAN_ENEMY build): ai_movement.cpp
  linked, the stand-ins for its functions left out (derived_ai.inc's UpdateMove, aicombat_test.inc's IsHumanWithin,
  aictl_test.inc's AdjustToPathHeight and Unknown5C, both sides). The enemy pair is the AI pair, its type set to the
  squad's, the scripted AI's, the sniper's or the neutral one's now and then (not the squad's for the driving:
  OnDriveArrived's GetOrderCount casts such a controller to a squad controller). HUMAN_AIMOVE_CASE runs one case alone
  (2,000 rounds, HUMAN_AIMOVE_ROUNDS more).
  - Faked on both sides and logged: the walking network's FindNearestType15, FindNearestFree, FindPath and
    FindEscapePath (16-bit "none" answers, as the network's: the original compares 16 bits); the body's movement
    stance; IsAISuspended; the team's selected soldier; the script's OnActionDone (its three copies); the door's
    GetSide; the car's SetInput and the vehicle's setters (part 34's loggers). GetOrderCount is human_control.cpp's,
    copied.
  - The state kept to the game's invariants: no two walking points at one place and none exactly at the human; a door
    point's actor its door, a ladder point's its ladder; the flags of a ladder or a door with their actors, the door's
    only on a route, a route followed with its waypoints; road paths of two points at least; the others near (not the
    human itself), none gone; the humans' frames up to date; the ladder's climbers on both sides (+0x160).
  - Shared harness changes: the waypoints compared field by field (the padding left out; g_waypointW: the positions' w
    not compared here, stale words in the original); the sight test's answer (derived_ai.inc g_daSight, part 45's) and
    the scene's sector by place used again; the seat car's original block 0xA00 bytes (its speed +0x958); the vehicle
    harness's copy of SegmentDistance2 summed as the port's now.
- **The tests:** 30,000 calls on UpdateMove, NeedsNewRoute, CheckCarsOnRoute, MakeWay, PlanRoute, RouteToPos,
  RouteToCheckpoint, RouteAwayFromEnemies, SmoothRoute, SmoothRouteTo, FixRouteHeights, StartRoute, FollowStep,
  UseLadder, PassDoor, ReachedWaypoint, FollowType15Route, FollowRoute (between doors, ladders and climbs),
  Climb, WalkToWaypoint (the march), UpdateMoveStance, IsOnPlainPoints, the avoidance, EndMove, IsMarchBlocked,
  UpdateCurrentCheckpoint, IsNearSegment, NearerPoint, AvoidOthersOnRoute (the other human on the way), IsHumanWithin,
  AdjustToPathHeight, Unknown5C, ClearRoute, SkipPassedWaypoint, SetRoutePoints, IsNearSelectedSoldier, MustSlowDown,
  HasAvoidance, CanAvoid, IsAwayFromOthers, UpdateDriving (the car standing, not getting nearer, reversing),
  OnDriveArrived, SteerForward and SteerReverse, FindActorNear and IsPosNearSegment.
- **Results:** 90,005 checks, 0 failures, 13 s (quick mode 7,500 rounds); every case also clean at 30,000 rounds alone;
  lines 1,068 of 1,072.
- **Port fixes:** float sums in the original's orders (from the x87 code; the port summed x, y, z): CutCorner (the
  projection x, y, z over the length z, y, x; the offset's length z, y, x, its normalization y, z, x),
  MoveWaypointToward (z, x, y), SegmentDistance2 (the length, the projection and the end distances z, y, x),
  GetDetourPoints (y, z, x), IsNear (z, x, y), FilterBlockers (z, y, x), UpdateDriving (x, z, y; the segment's
  remaining distance y, z, x; the distance to its end z, y, x). Compare semantics: AdjustToPathHeight and Unknown5C keep
  a NaN position along the segment (two waypoints one above the other, as a ladder's) as the original's "not below 0"
  tests do; UpdateDriving's moving test counts a NaN speed (no time passed, no move) as moving; IsNear includes 0.9 m.
  EndMove reads the body's climb state as the original (no change in behaviour).
- **Not covered:** the port's fixes (one-point road paths, an empty route in SmoothRouteTo), OnDriveArrived's squad
  branch, GetDetourPoints' second root below 0. Not tested: a human standing exactly on a walking point (the corner cut
  divides by zero on both sides, and the NaN goes on into S_AIFacing::SetFacing, where the port and the original take it
  differently: worth a look with ai_parts.cpp).
- **Original flaws, steered clear of (the port fixes them):** IsOnPlainPoints reads the first waypoint of an empty
  route; FilterBlockers takes gone actors and other kinds for humans; FollowRoute takes a door point's actor for a door
  unchecked; UpdateDriving reads a one-point road path past its end; SmoothRouteTo reads an empty route;
  RouteToCheckpoint and NeedsNewRoute ask a missing network. BeginRoute keeps the door's flag with the door cleared
  (FollowStep would then pass a null door): unreachable in the game (the flag is only set on a route, and Finish clears
  both).
- **Mutations:** 1,178 mutants (1,042 T, 136 R); 86 not building; 942 of the 1,092 building killed (86 %), 150 survived:
  pool releases and stale words not observable, ties at the thresholds, weakly tested combinations (the detours'
  geometry, the driver's resets), the squad branch and the port's fixes (see mutate_aimove.py).
- **Harness lessons (my mistakes):** "none" answered as -1 by my network fakes (the original compares 16 bits); several
  random states the game never has (two points at one place, a point on the human, a ladder flag without a ladder, a
  door flag without a route, the AI's own human among its nearby actors, a squad type on another controller, the
  original's "gone" actor id); the ladder's climbers 4 bytes off; the seat car's original block too small; one copy of
  OnActionDone of three redirected; the port's S_vector::Dot read from the wrong header (ls3df's z, y, x where the
  game's sums x, y, z), which cost a long detour hunt; an InsertDetours early return I added from a misread of the asm
  (its success path jumps past the decrement), taken out again when the long runs showed it; heredocs collapsing "\n"
  again.

## Human part 47 (the controls, `input_controls.cpp`, 2026-11-18)

- **The build:** `controls/build_diff.bat` (`controls_diff.cpp`, a new harness beside part 11c's expected-value test in
  script_module): input_controls.cpp linked, the original executable mapped (collision/original_exe.cpp). The port's
  C_Controls and the original's object (a raw block: MSVC6 vectors of 16 bytes) get the same random state before each
  call (the original's vectors rebuilt in fresh buffers, now and then with spare room; its operator new / delete the
  test's malloc / free) and are compared field by field after it, with the calls each side made on the fakes. The
  graphics interface is one fake for both sides (stdcall slots); the joysticks one set per side (the original's vtable
  the port's a slot down: g++ puts two destructors first). The render callback: per-side fake games (the original's
  GetGame, GetGEffects, GetIGraph, the interface's Render / RenderOverlay, the quick messages, ApplyFade, the debug
  statistics and drawing redirected to loggers; the interface's IsActive and the script module's two cutscene bytes the
  original's own). HD2_CONTROLS_ROUNDS (default 20,000 per kind), HD2_CONTROLS_QUICK (4,000), HD2_CONTROLS_CASE (one
  kind), HD2_CONTROLS_DUMP (the bindings or axes that differ).
- **The tests:** Update, PollPress (the filters, the keyboard's modifiers and typed keys, the mouse's buttons and moves,
  the joysticks' buttons, hats and axes in every axis mode), Bind, SetBinding (the keys' 64 slots full, none, out of
  range), ClearAction and ClearKey (own arrays and the caller's), GetValues, GetValue, SetMouseScale, SetInvertMouse (a
  byte store in the original), Attach and BindControls, InitControls, Shutdown, the constructor and destructor,
  WideToAnsi (non-ASCII text, short buffers, nulls; the last error too) and GameRenderCallback (each phase, the in-game
  menu's state, cutscenes, the fade's states and end delay); then a run of mixed calls on one object. States: bindings
  made through SetBinding (the watched keys and counts agree), then now and then disturbed (keys, counts, a joystick
  binding without its joystick); hat angles and axes at the thresholds and a step either side, NaN now and then.
- **Results:** 7,351,028 checks, 0 failures, 18 s (100,000 rounds per kind also clean); lines 338 of 338.
- **Port fixes:** HatDirections in the original's compare forms (a NaN angle is down: the port said up-left);
  PollPress's axis thresholds are the floats 0.3f, 0.5f and 0.7f (the original's doubles hold the floats' values: an
  axis at exactly 0.3f did not count as moved there, but did in the port) and its direction tests "not above" (a NaN
  axis is negative). Neither matters with a real joystick (IJoy's angles come from hundredths of degrees, never NaN;
  an axis exactly at 0.3f is rare) but both are now the original's.
- **Not covered:** nothing in this file. Not tested: hats 4..15 in a binding's key (the original and the port read past
  the four hats alike: no game binding has one).
- **Mutations:** 350 mutants (320 T, 30 R); 21 not building; 311 of the 329 building killed (95 %), 18 survived, all
  equivalent: the dedicated server's branches (not compiled here), the unused global, ties and unreachable variants
  (see mutate_controls.py). A second pass added Attach over another graph and the watched keys' 63 slots full.
- **Harness lessons (my mistakes):** the original-side joysticks' vtable left with a null destructor slot crashed the
  run at exit (the static objects' destructors); a sed edit on a script with backslashes and the session path (its
  "46" replaced) again.
- **Shared harness fix (found by this part's suite run):** human.sniper failed in this checkout since part 46 (1 saved
  game of 894 differing) but passed in a fresh worktree of the same commit, so part 46's "all suites pass" was wrong.
  The AI parts' copy into the original (aictl_test.inc AiToOrig) maps every word that equals a port pointer; the move
  order's checkpoint word (+0x20: 16 bits and stale padding) once made such a value, by the heap's addresses (hence the
  path), and was handed over as the original's pointer. That word is now copied and compared raw; sniper_test.inc
  prints where the saved games differ.

## Human part 48 (the controls' base, `control.cpp`, 2026-11-18)

- **The build:** `controls/build_base.bat` (`control_diff.cpp`): control.cpp and c_str.cpp linked, the original executable
  mapped. The port's controls are real C_Control objects in a random tree (up to 10, ids by the children's fields, a
  stray id, duplicate or -1 now and then; routes, callbacks, the any-callback); the original's are raw 0x6C blocks with
  its own vtable (0x00813dd0: its virtual calls stay in its code), built from the port's before each call (MSVC6 vectors,
  the routes' texts reps of their own) and compared field by field after it, with the message queue (the original's
  MSVC6 deque at 0x008ae590: 170 messages a block, read and emptied by hand), the frames' data and the calls on the
  fakes: the frames (one set, stdcall slots) and the callbacks (a cdecl fake on the port's side, a thiscall one on the
  original's, the object in ECX and again as the argument: mapped to each other). The factory (CreateChild) is faked on
  both sides: its controls are controls.cpp's (later parts). Self routes that raise notifications (show, hide, notify)
  are kept off cycles. HD2_CONTROL_ROUNDS (default 20,000 per kind), HD2_CONTROL_QUICK (4,000), HD2_CONTROL_CASE,
  HD2_CONTROL_TRACE (rounds printed), HD2_CONTROL_FIRST_CHANCE (faults under the original's exception frames).
- **The tests:** FindChild, FindControl (up, down, the top bits), IsChild, TakesInput, Show, Hide, Refresh, SetInactive,
  the trivial slots, the input slots (each notifies; OnKeyClick then clicks), HandleMessage (every code, routes added,
  the get messages' values back), HandleMessageCopy, HandleMessages, Notify (callbacks, then routes posted or handled),
  CallCallbacks, AddCallback, RemoveCallback (by object, notification, function, the any-callback), Create (the frame's
  data, the type, a child per description), the constructor and the destructor (a subtree), the descriptions
  (constructor, AddChild past 255 children, the four AddDescMessage, the destructor) and PostControlMessage (past a
  block).
- **Results:** 16,988,776 checks, 0 failures, 9 s; lines 226 of 226.
- **Port fixes:** none. The original's refresh message to the children carries two stack words (value2, value3; the
  port's are 0): never read. The original's text AddDescMessage takes its C_str by value (the port's a reference): the
  same.
- **Mutations:** 172 mutants (150 T, 22 R); 4 not building; 164 of the 168 building killed (98 %), 4 survived, all
  equivalent: the destructors' deletes (a leak) and RemoveCallback's match reset (see mutate_control_base.py). A second
  pass logged Refresh and HandleMessageCopy on both sides (the port's a C_LoggedControl, the original's a copy of its
  vtable): the refresh's recursion into the children was not observable with base controls.
- **Harness lessons (my mistakes):** a vectored handler written as a lambda (not stdcall) broke the build, and I ran the
  stale executable for several rounds of tracing; a generator branch gave notify routes a callback's values, so a self
  route notified itself (a stack overflow, no crash report: the first-chance handler found it); the original's text
  AddDescMessage called with a reference to its C_str.

## Human part 49 (the small controls, `controls.cpp` lines 135-765, 2026-11-18)

- **The build:** `controls/build_small.bat` (`small_diff.cpp` with the kit `controls_kit.inc`, for the controls parts to
  come): controls.cpp, control.cpp and c_str.cpp linked, the original executable mapped. What controls.cpp reaches outside
  the controls is faked on both sides with one set of loggers:
  - the engine's objects (frames, visuals, the mesh, its level, face groups, materials, textures, the graphics
    interface, its image, the font manager): fakes in one arena both sides use (stdcall slots, the engine's field
    layout), reset before each side's call and compared byte for byte after it (the frames' places, flags and data);
  - the text boxes: the port's C_TextBox defined in the kit, the original's methods redirected to the same loggers (its
    vtable 0x00810458's slots too: the original's text control makes its box inline, the screen's size and the font
    manager asked there, as the kit's port constructor does); the fields the controls set inline compared at the
    original's offsets (the runs' count, dimmed, margin, colour, the screen's size, the font manager);
  - the cursor: one block (the still time at +0x40, read inline by the original), Acquire, Release, IsButtonDown and
    GetPosition logged;
  - everything else controls.cpp names (the paged boxes, key bindings, texts, the network game ...): stubs that fail the
    test when reached (later parts fake what they use); its globals raw storage (controls_data.cpp).
  The controls: random trees of the base, text controls, titles, labels, pictures, groups, frame switches, text buttons,
  tooltips and buttons (the port's wrapped in C_Logged<T>, the original's with a copy of the class's vtable: Refresh and
  HandleMessageCopy logged), compared field by field (part 48's base, each class's own at the original's offsets) with the
  message queue after each call. HD2_SMALL_ROUNDS (default 20,000 per kind), HD2_SMALL_QUICK (4,000), HD2_SMALL_CASE,
  HD2_SMALL_FIRST_CHANCE.
- **The tests:** every slot without a message (the input slots, OnWheel, Tick, Unknown20, Refresh, SetInactive, Show,
  Hide, IsChild, TakesInput, FindChild), HandleMessage and HandleMessageCopy with the base's and the text, picture and
  frame switch codes (the get messages' values back), Create (with and without a description; the button's three frames,
  the text button's two, the picture's face group, the tooltip's ticker) and the destructor (a subtree: the boxes deleted,
  the cursors released), ticks on trees of tooltips, buttons and groups (the still time at its thresholds).
- **Results:** 5,571,936 checks, 0 failures, 3 s; lines 135-765 all run.
- **Port fixes:** none. The original's tooltip announces itself with a message whose value3 is a stack word (the port's
  0, its comment's fix): not compared (the screen reads value2).
- **Mutations:** 192 mutants (79 T, 113 R); 6 not building; 178 of the 186 building killed (96 %), 8 survived: the
  dedicated server's branches, the frame data set twice, and three constructors' m_takesInput defaults (the original's
  constructors are inlined in its factory: that part's). A second pass gave a third of the controls the catch-all
  callback (Notify was not observable otherwise), logged Tick and added the ticks' case.
- **Harness lessons (my mistakes):** the original's text control makes its text box inline (not through the box's
  constructor I had redirected), so its first run went into the real font manager and the real box's vtable; text
  controls without a box and face groups without a material crash both sides (the game never has them); the picture's
  SET_TEXTURE (it shows or hides) generated as a quiet route message, a self route that showed itself for ever.

## Human part 50 (the paged text, slider, scroll bar and spin controls, `controls.cpp` lines 776-1528, 2026-11-18)

- **The build:** `controls/build_medium.bat` (`medium_diff.cpp` with part 49's kit, whose trees, messages and tests now
  live in `controls_tree.inc` for every part: the part gives its kinds, its quiet and loud codes, its ticking kinds and
  hooks for a message's values, a tree's fix-ups, a Create's frames, the messages a node takes and the codes routes never
  carry). New in the kit: the frames' bounds, type and the visuals' world bounds (the arena's), UpdateBound and LS3DF's
  UpdateWMatrixProc logged (the original's import 0x007e5296), the cursor's GetMove, the box's hover, the paged box (the
  original makes it inline as the text box; its vtable 0x008154a4's Render and destructor, AddArea, SetPage, Relayout,
  LastPageBreak redirected; NumPages and Reattach the original's own), the game's texts' Get (its C_str through a hidden
  pointer), and the controls a call makes adopted into the trees (the scroll bar's and the spin control's arrows, the
  scroll bar's slider: paired by their place among the children, their kind by the original's vtable).
  - The scroll bars have their arrows and (mostly) a slider, the spin controls their arrows and an item: the original
    without them crashes (the port's fixes check them).
  - Not compared: the value2 / value3 the original leaves unset in the messages it builds on its stack (the tickers, the
    arrows' and the slider's routes, the slider's set and get, the set position: the port's 0, by its comment's fix;
    their readers never look), the repeat time of a button the call made (the original's inlined constructor leaves it
    unset; read only after OnPress sets it).
- **The tests:** part 49's cases (every slot, the messages, Create, the destructor, the ticks) on trees of these kinds,
  the slider's drags and SetLimits (through Create, its frames' bounds), and the scroll bars' own messages sent to them
  (up and down past the ends, the count, the position, the page step, the slider moved; ticks at the repeat time's
  end, presses toward a cursor before or just inside the start, square bounds, long lists), the slider's value at its
  clamps and NaN, and the spin control's own messages (the wraps, the removal's clamp, the choice by index).
- **Results:** 17,999,364 checks, 0 failures, 8 s (the message, slot, tick and drag cases also clean at 100,000 and
  200,000 rounds alone); lines 776-1528 all run.
- **Port fixes:** the scroll bar's step (100 / (count - 1)) is rounded once, as the original's (the count loaded
  exactly, then divided in single precision): the port converted the count to a float first, a second rounding for
  counts from 2^24 (never in the game; 1 ulp). Below that the float division is kept (bit for bit the same); above it,
  double (its own second rounding is possible only at an exact tie of the double quotient: not seen).
- **Original flaws, steered clear of (the port fixes them):** the spin control without items steps to the item past the
  end; the scroll bar without arrows (SetInactive) or a slider (GET_SLIDER) crashes; SLIDER_MOVED without a slider
  crashes both. The random states keep to what the original survives (a route emptying a spin control's items is never
  generated).
- **Mutations:** 413 mutants (285 T, 128 R); 9 not building; 379 of the 404 building killed (94 %), 25 survived, all
  equivalent: clamps at a tie that assign the value they test, unreachable states, Reattach (empty on both sides), an
  Update after SetInactive(true), [[fallthrough]] (see mutate_controls_medium.py). Further passes per the survivors:
  the stack words' allowance per field (it had hidden a ticker's value2), the pages control without its box, odd divisors
  past 2^24, the Slide and Spin cases, the scroll bar's ticks, presses, square bounds and long lists, the slider's base at
  its limits.
- **Harness lessons (my mistakes):** the cursor's GetPosition and GetMove return their out pointer (the original's
  callers read it through EAX: my fakes returned nothing); a scroll bar's step of 0 (never made by SET_COUNT) sent an
  infinite place to the original's own CRT floor, whose error path needs the CRT the test never starts (a crash in
  ntdll); the arena's world bound written with its max at +0x0C (I3D_bbox pads its vectors: +0x10, the
  original's and the port's); a spin control's box taken away for SetInactive, which uses it unchecked on both sides; the heredoc editing
  scripts again (Write and a script file instead).

## Human part 51 (the check boxes, toggle buttons, side indicator and mark, `controls.cpp` lines 1529-2199, 2026-11-18)

- **The build:** `controls/build_check.bat` (`check_diff.cpp` with the kit): the check box, the toggle button and the
  "all" one, the highlighted check box and its halves, the side indicator, the mark (and the title it owns). New in the
  kit: the child frames' names as a table (32), a multiplayer zone in the arena (its team at the original's +0x98 and at
  the port's own offset: g++ lays C_MPZone out its own way), the frames' LinkTo logged, the original's C_fstr
  (0x00405100: formats as the port's), and controls a kind owns outside its children (the mark's title: the original's
  offset, the port's field) followed by the teardown, the destructor's walk and the adoption.
  - The highlighted check boxes have their halves, the marks mostly their titles; Create's frames have the children
    their Create asks for (the side indicator's and the highlight's halves are used unchecked by both sides).
- **The tests:** part 49's cases on trees of these kinds, their own messages sent straight to them (checked, quiet,
  the value's byte against the state, the side, the mark's number, the highlight's turn), and the halves' and the check
  boxes' slots (over or not, empty or full, inactive).
- **Results:** 9,365,671 checks, 0 failures, 6 s (the messages, own messages and halves' cases also clean at 100,000
  rounds alone); lines 1529-2199 all run.
- **Port fixes:** none.
- **Mutations:** 323 mutants (199 T, 124 R); 15 not building; 305 of the 308 building killed (99 %), 3 survived, all
  equivalent: two [[fallthrough]] deletions and the new no half's SetFull(false) (it starts empty; see
  mutate_controls_check.py).
- **Harness lessons (my mistakes):** the mark's title deleted twice by my teardown (its destructor deletes it, and it
  was a node too); the zone's team written only at the original's offset (the port read its own: a difference that was
  the harness's).

## Human part 52 (the edit control, edit box and selection list, `controls.cpp` lines 2200-2764, 2026-11-18)

- **The build:** `controls/build_edit.bat` (`edit_diff.cpp` with the kit). New in the kit: the text box's editing,
  selection, hover, runs and scroll (EditRun, TickCaret, SelectHovered, GetSelection, SelectRun, GetScrollRange, HoverAt,
  InsertText, SetSelectedText; GetSelectedText, GetRunText and GetLineCentre returned through a hidden pointer: a round's
  answers on both sides; the box's scroll compared), the cursor's screen place, EncodeUtf8 (the original's own on both
  sides: text_box.cpp's is a later part's), the original's allocations zeroed, "select*" among the child names, and the
  notifications loud self routes are on set by the part (here 05, 0B and the click: ending an edit notifies 02).
  - The profile's unicode font a round's (the port's member, the original's byte at 0x008aea0c + 0x30); the original's
    buffer (a char *) holds the port's bytes.
  - The original's line arguments to EditRun, SelectRun and GetLineCentre are 16 bits (MOVSX of a word; the word above
    the caller's stack): logged as 16 bits on both sides. The selection list's set count and the editing's start and
    stop messages leave stack words (value2 / value3) in the original: not compared there (part 50's allowance).
- **The tests:** part 49's cases on trees of these kinds, typing (OnChar while editing: Backspace, Tab, Return, Escape,
  printable characters, digits only, the forbidden ones, full-width digits, unicode codes; the buffer's UTF-8 of 1 to 4
  bytes, odd tails and the leads at their ranges' edges, stale bytes after the text, nearly full buffers), the editing's slots (BeginEdit, IsEditing, OnInput02, OnInput0B, OnKeyClick) and the kinds' own
  messages sent straight to them (forbidden, longest, digits, edit, get text, end edit, the range and the number, clear,
  items, insert first, remove selected, the position, select text).
- **Results:** 8,370,627 checks, 0 failures, 7 s (typing, own and message cases also clean at 100,000 rounds alone); lines
  2200-2764 all run.
- **Port fixes:** none. Original flaws steered clear of (the port fixes them, by its comments): a new edit buffer not
  cleared, the buffer shown through C_fstr (a '%' reads stray arguments), Backspace reading before the buffer at a lone
  continuation byte, the 4-byte encoding buffer for 6, isdigit given a signed byte.
- **Mutations:** 272 mutants (196 T, 76 R); 7 not building; 254 of the 265 building killed (96 %), 11 survived, all
  equivalent (see mutate_controls_edit.py). Two more passes per the survivors: stale bytes after the texts (the
  terminators' writes and BeginEdit's clearing were not observable), backspace often with the UTF-8 leads at their
  ranges' edges, full buffers, key 0x7F, byte-mode keys of a 0 byte, the port's buffer's size checked, a range upside
  down.
- **Harness lessons (my mistakes):** the original's 16-bit line arguments taken for ints; the buffers' generator almost
  never ended a text in a multi-byte sequence (the backspace's cases not run until it did), and left nothing after
  the text's terminator (as a backspace does); a heredoc with backslashes once more (Edit instead).

## Human part 53 (the scrolled text, the combo boxes and the chat input, `controls.cpp` lines 2765-3362, 2026-11-18)

- **The build:** `controls/build_group.bat` (`group_diff.cpp` with the kit). New in the kit: `controls_kinds.inc` (parts
  49-52's kinds' helpers for the later parts' groups), a tree plan hook (each group with its parts as its Create makes
  them, now and then one left out with a plain control in its place, two swapped or an extra child; the scroll bars with
  their arrows and slider), nine more child frame names (drawn only when a part asks: the earlier suites' random streams
  kept), the controls' own callbacks mapped to the original's (the header's edit 0x00658930, the chat line 0x006571f0;
  callback objects mapped as nodes), a callback value's text (the chat line's SEND carries a pointer to its text) and
  per-part stack-value codes (the routes Create adds and the chat line's EDIT carry stack words in value2 / value3: in
  one, LineEntered's own address).
- **The tests:** part 49's cases on the planned trees; the groups' own messages straight to them with targets of any last
  4 bits (to a part or not); their input slots with such ids and the wheel at its 120 steps' edges; SetStrings (the
  lists' list taken away now and then); the callbacks called straight (the header edited; the chat line with spaces
  first, commands, empty, bytes past 0x7F); a titled combo box without a part (FindChild, SetInactive).
- **Results:** 21,827,362 checks, 0 failures, 12 s; lines 2765-3362 all run.
- **Port fixes:** two calls made as the original's: LineEntered's prompt and the chat input's oldest line removed went
  through HandleMessageCopy; the original calls HandleMessage on its stack message (not observable in the game, only in
  the logged calls). Original flaws steered clear of (the port checks the parts, by its comments): the scrolled text's
  list, the combo box's three parts, the titled combo box's two and the chat input's label used unchecked; and the
  game's frames always have the parts FindChild names (FindControl uses its indices unchecked, on both sides).
- **Mutations:** 217 mutants (160 T, 57 R); 5 not building; 200 of the 212 building killed (94 %), 12 survived, all
  equivalent (see mutate_controls_group.py). One more pass per the survivors: a titled combo box without a part.
- **Harness lessons (my mistakes):** missing parts left FindChild naming children that did not exist (a crash in
  FindControl, the trees' and not the port's); the scroll bar's arrows wired by their ids' last bits (under a scrolled
  text they are the bar's | 1 and 2, its own bits 4 .. 7); a selection list given to LineEntered (its GET_TEXT answers
  no text, read unchecked by the original); the buffer set for the chat line where GET_TEXT answers the box's selected
  text; a heredoc with backslashes once more (Edit instead); a survivor list misread (the flipped comparison was killed,
  its id twin survived).

## Human part 54 (the skill, `controls.cpp` lines 3363-3622, 2026-11-18)

- **The build:** `controls/build_skill.bat` (`skill_diff.cpp` with the kit). New in the kit: the skill's two frame names
  (after the others: the earlier suites' random streams kept; the group suite draws its 42 as before).
- **The tests:** part 49's cases on planned trees (one to three skills with their two titles, their buttons now and then
  missing, two parts swapped or a child added); the skill's own messages straight to it (SKILL_UP / DOWN, SET_BASE,
  SET_TOTAL past 0 and 100, GET_NUMBER, SET_TEXT, the rest passed to the titles); its input slots with ids of any last 4
  bits and its keys (minus, plus, the keypad's, neighbours, bits above the byte); SetStrings. Each skill's pool (its
  value: the control asked for a point): none, a control that is not a skill found from every skill, or another skill
  found as itself whose pool is not a skill (SET_VALUE's values drawn from the same; no SET_VALUE routes).
- **Results:** 18,446,276 checks, 0 failures, about 15 s; lines 3363-3622 all run (158 of 158).
- **Port fixes:** none. Original flaws steered clear of (the port checks them, by its comments): the titles used
  unchecked in Refresh, SetInactive, Update, HandleMessage and Create (both buttons there), and the pool FindControl
  finds used unchecked in PointUp / PointDown. Not tested: two skills each other's pool (both sides recurse without end;
  the game's pool is one skill with none).
- **Mutations:** 76 mutants; 3 not building; 68 of the 73 building killed (93 %); 5 survived: 2 the item information's
  constants (the next part's), 3 equivalent (see mutate_controls_skill.py).
- **Harness lessons (my mistakes):** Create's button routes carry the original's stack words in value2 / value3 (as in
  part 53: SKILL_UP / DOWN made stack-value codes); a vector given to the kit's array Pick.

## Human part 55 (the item information, `controls.cpp` lines 3623-3793, 2026-11-18)

- **The build:** `controls/build_info.bat` (`info_diff.cpp` with the kit). New in the kit: a GetItemDef hook (g_itemDef),
  GetImage's cached path (its picture or none without a name; loading is item_defs.cpp's part), two frame names (after
  the others: the earlier suites' streams kept, the skill suite drawing its 44). The test defines the item definitions'
  out-of-line members (item_defs.cpp is not linked) and builds each round's item table on both sides: a few ammo,
  animated and static definitions at random ids (0 and 499 often), the original's in its table at 0x0098b030 with its
  vtables, so its virtual calls (behaviour, ammo amount and item, headgear) are its own; their components (projectile
  weapons, firearms, scopes: muzzle velocities, aim modes, field of view factors) and cached pictures.
- **The tests:** part 49's cases on planned trees (one or two informations under a plain control, each with its two
  pictures and its stats' title, now and then two swapped or a child added); its own messages straight to it (SET_ITEM
  with ids set, empty, negative, past 499; the counts, the callbacks, REFRESH); its input slots with ids of any last 4
  bits.
- **Results:** 12,787,463 checks, 0 failures, 8 s; lines 3623-3793 all run but ShowPicture's return without a picture
  (119 of 120: the original uses its pictures unchecked).
- **Port fixes:** none. Original flaws steered clear of (the port checks them, by its comments): the pictures, the
  stats' title and the parent (told of the counts) used unchecked; in Create the frame's ammo child looked up on a null
  frame (both sides: the tests give one always).
- **Mutations:** 49 mutants; 3 not building; all 46 building killed (one more pass: rifles with a scope in aim mode 4
  drawn more often).
- **Harness lessons (my mistakes):** the information's vtable is not in its destructor (the compiler left the group's
  there): found by the slots holding its FindChild and HandleMessage; the zoom's limit too rare to be drawn at first; a
  heredoc with a backslash once more (its result right, checked).

## Human part 56 (the list group and its items, `controls.cpp` lines 3794-4224, 2026-11-18)

- **The build:** `controls/build_list.bat` (`list_diff.cpp` with the kit). New in the kit: nodes deleted during a call
  (an item removed, a list cleared): the port's recorded by C_Logged's destructor, the original's by its operator delete
  on a test block (not freed there: ToOrig frees it), checked to be the same nodes and dropped before the trees are
  compared; CreateChild's log with its root frame and paired descriptions named alike (g_fnPairs: the original's ADD_ITEM
  description built beside the port's); the selector's frame name (the earlier suites' streams kept). The original's
  _aligned_free import made a no-op (the origin is freed by the port's destructor).
- **The tests:** part 49's cases on planned trees (one or two lists of up to 8 items, items sharing an id now and then,
  an extra control owned by the list, origins, steps, selector frames, cursors); the list's own messages straight to it
  (REMOVE_ITEM, ADD_ITEM with its description, CLEAR, the scroll ones, SET_POSITION with negative values on lists with
  items, SET_ORIGIN, the steps, REFRESH, SELECT_INDEX); its and its items' input slots, the navigation keys, the wheel at
  its 120 steps' edges (the value naming a node: the scroll bar paged), ScrollRange.
- **Results:** 13,573,775 checks, 0 failures, 8 s; lines 3794-4224 all run (259 of 259).
- **Port fixes (four, all observable in the game's lists):** REMOVE_ITEM's scroll back compares the items left past the
  first shown unsigned (a first past the items scrolled back to 0 in the port; the original stays); SET_POSITION compares
  unsigned (a negative position is past the end: the original scrolls on to the end, the port scrolled back); the sort
  compares the ids signed (ids with the top bit set first, as the original's); the selector's depth is a float 0.1
  (FSUB float ptr [0x0080f550]; the port subtracted a double and rounded twice: one ulp off now and then).
  vehicle_setup/list_test.inc expected the port's former SET_POSITION -1 (staying at the start): now the original's (on
  to the end, then back with SET_POSITION 0).
- **Original flaws steered clear of (shared, game states the tests keep to):** Select loops for ever when the last shown
  is negative (SET_VISIBLE 0, or a last below the first: Scroll on never passes the unsigned compare) and an empty list
  scrolls on without end to a far or negative SET_POSITION; the tests keep the last at first + visible - 1, visible at
  least 1, SET_VISIBLE sent at the first, negative positions only to lists with items. An item's click takes its parent
  to be a list (items only under lists).
- **Mutations:** 140 mutants; 5 not building; 128 of the 135 building killed (95 %), 7 survived, all equivalent (see
  mutate_controls_list.py). One more pass per the survivors: items sharing an id.
- **Harness lessons (my mistakes):** the shared infinite loops above found as hangs first (the cases bisected by rounds,
  then traced); the list's value never naming a node (the wheel's pages unrun) until AfterTree drew node ids; the earlier
  rounds' bad_alloc was the empty list's endless scroll filling the call log.

## Human part 57 (the bar and the upgrade bar, `controls.cpp` lines 4225-4411; the original CRT's printf, 2026-11-19)

- **The build:** `controls/build_bar.bat` (`bar_diff.cpp` with the kit). New in the kit: the upgrade bar's frame names;
  g_afterOrig (a part's fix-up after the original's call: the bars put back the scale's fourth word, the original's
  SetScale copies 16 bytes from its 12-byte vector, the last its stack's); S_quat::Make LS3DF's on the port's side (the
  original's SetScale calls it); the original's C_fstr stand-in (O_Fstr) now formats with the original's own CRT
  (_vsnprintf 0x007e5ad2 after its _cfltcvt_init 0x007e599c: MinGW's before).
- **The tests:** the earlier parts' cases on planned trees (bars, upgrade bars with their two titles, under a control
  or alone); the bars' own messages straight to them (the gets and sets of min, max, value, axis; the upgrade bar's
  value, upgrade and suffix; its text messages; a title taken away for the bar's messages); UpdateBar and Refresh
  straight, values half way between the shown tenths; Format: the port's formatter against the original's _vsnprintf
  (ties at the precisions, big values, random bit patterns, infinities and NaNs, flags, widths, precisions, '*', the
  integers, I64, %p, %n, an unknown conversion, a buffer too small).
- **Results:** 10,055,593 checks, 0 failures; lines 4225-4411 128 of 128 measured lines run (msvc_printf.h 236 of 237: the one left S_Flt's declaration, gcov's line for its inlined constructor). Format: values tied past the 17th significant digit
  counted, not compared (the original's 80-bit digits go either way there; their texts differ only where 17 or more
  digits are shown, values of 1e14 and up printed whole: no format of the game's).
- **Port fix (observable in the game):** the original's statically linked MSVC6 CRT formats floats unlike today's
  (UCRT, MinGW): the decimal digits rounded half up (12.5 "%3.0f" "13", the port "12"; 0.25 "%.1f" "0.3", 1.25 kg
  "1.3"), 17 significant digits then zeros, a zero's sign dropped ("0.0"), three exponent digits, "1.#INF", "1.#QNAN",
  "1.#IND" digit-rounded ("1.$"), a signalling NaN quieted, a '#' point on %g, an unknown conversion its character
  alone. New `common/msvc_printf.h` (msvc6::VsnPrintf / SnPrintf, header only, exact digits by integer arithmetic)
  used by C_fstr (the weights, the equipment screen's stats, the upgrade bar), PrintMessage, PrintText,
  PrintNamedText and GameSpy's %f. The bar's port itself: no differences (its compares, NaN paths included, as the
  original's).
- **Not tested:** the bars with NaNs of different signs on both sides of a division (x87 and SSE pick differently: the
  tests draw the one NaN arithmetic makes); range overflow (the original's x87 keeps a wider exponent); the original's
  crashes the port's checks avoid (an upgrade bar without its name's title taking a text message, without either title
  refreshed); %lld and %zu (C's in the port; MSVC6 knows neither, the original uses neither).
- **Mutations:** the bars 67 mutants, 2 not building, 63 of 65 killed, 2 equivalent (mutate_controls_bar.py); the
  formatter 334, 19 not building, 275 of 315 killed, 40 equivalent (mutate_msvc_printf.py). Per the survivors: the
  formatter's redundant code taken out (a %g carry's resize, a layout flag, a sign skip, %I32d), the tests given
  widths with 9, %.0d of 0, %I32d, the doubles whose 17 digits carry to a power of ten (1e-14 and others), small
  buffers.
- **Harness lessons (my mistakes):** the kit's MinGW stand-in for the original's C_fstr hid the CRT difference until
  the original's _vsnprintf was called directly (the first check compared the port with itself); MinGW's printf gives
  wrong digits under the game's 24-bit x87 precision (62789200642048 printed 62789199551562.718), so the formatter
  makes its digits with integers; the original's %f traps until its CRT start-up's _cfltcvt_init has run; three new
  frame names took the kit's arena past its 48 names a frame (four controls suites crashed until the full run: widened,
  a static_assert now); the vehicle harness's hand-written texts had MinGW's rounding (0.25 "+ 0.2", 12.25 kg "12.2",
  612.5 "612", 64.5 " 64"): now the original's ("+ 0.3", "12.3", "613", " 65").

## Human part 58 (the hiding list and the pool list, `controls.cpp` lines 4411-4542, 2026-11-19)

- **The build:** `controls/build_hide.bat` (`hide_diff.cpp`: part 56's list suite with the two kinds). New in the kit:
  S_Kind::heldAt / heldVec (controls a kind owns in a vector outside its children: the hiding list's hidden items at
  +0xA0), walked by ClearPortTree and the destructor's test on both sides (a hidden item deleted alone taken out of its
  list's vector).
- **The tests:** planned trees of lists of the three kinds, the hiding and pool lists with hidden items (items, now and
  then a label; ids now and then a child's), the pool's held item (a child, a hidden one, none); the earlier parts'
  cases on them; their own messages straight to them (ITEM_COUNTED of hidden items, children and others, HIDE_ITEM,
  HIDE_ALL, the pool's ADD_ENTRY and ADD_CURRENT_ENTRY with maps: the port's C_MpMap and the original's block, its name
  at +0x01, paired as the descriptions are, none now and then; a pool's ADD_ITEM code is its ADD_ENTRY); the pool's
  inputs 08, 09, 0A; Construct: the constructors' fields as the original's factory (inline there) sets them.
- **Results:** 17,957,625 checks, 0 failures; lines 4411-4542 all 81 measured lines run. The port: no differences. HIDE_ALL's
  scroll loop signed and HIDE_ITEM's unsigned, both the original's (a negative first shown would loop for ever: nothing
  sets one in these lists).
- **Original's flaw kept (both sides):** an item removed or the list cleared while the pool holds it (a drag) leaves the
  held pointer dangling (the test accepts it deleted on both sides).
- **Not tested:** the hidden controls' OnLeave id (see the mutations); the factory's own construction (redirected by the
  kit: its fields checked from its asm).
- **Mutations:** 37 mutants, all building, 35 killed, 2 equivalent (mutate_controls_hide.py).
- **Harness lessons (my mistakes):** the plan's hidden flags outlived their tree (the ticks' trees are not planned: a
  crash until AfterTree cleared them); ADD_ITEM among the loud codes reached pools as ADD_ENTRY with a random value2
  (the original faulted reading its map: the codes now sent only by the Own case); Edit dropped a trailing space in the
  summary line ("hide_diff:N checks").

## Human part 59 (the server list and its rows, `controls.cpp` lines 4558-4901, 2026-11-19)

- **The build:** `controls/build_server.bat` (`server_diff.cpp` with the kit; links `mp_text_ids.cpp`, the kit's
  GetGameTypeTextId stub left out by CONTROLS_KIT_HAS_GAME_TYPE_TEXT). New in the kit: 17 frame names (the rows' "Server1" .. "Server11", the six icons;
  S_Arena::child widened to 72); IsLegalUtf8Range and HasUtf8Sequences the original's own on the port's side (text_box.cpp
  is a later part's). The suite redirects the original's _aligned_malloc (0x007e53c2: its CRT heap never made in the
  copy) to MinGW's.
- **The tests:** planned trees of server lists (up to 11 rows, mostly 11; the rows' ids as Create gives them, their
  callbacks to the list: the port's RowClicked / RowNotified paired with the original's 0x00666650 / 0x00666630), a
  round's pool of 300 server entries (the same blocks on both sides: names and maps with case ties, '_' and brackets,
  UTF-8 valid and not, full length; game types out of range; pings, players and maximums with ties and 32-bit values;
  versions the round's or not; the unicode font on or off); lists of 0 to 250 entries, selections in and out of them,
  sort keys of every field and direction (other bits too), windows in, past and before the entries. The kit's cases on
  them; the list's own messages straight to it (ADD_ITEM with entries or none, CLEAR, SET_POSITION within, past,
  negative, near INT_MAX, SORT, REFRESH, the gets) and the rows' (ADD_ENTRY with entries or none, ADD_CURRENT_ENTRY,
  REMOVE_ITEM, GET_ITEM, run texts); the inputs (a row's click: RowClicked, the list's refresh and notification);
  Create (the rows made, their icons found or not, the origin from the last row's place); Construct (the factory's
  fields from its asm, the row's).
- **Results:** 24,510,617 checks, 0 failures; lines 4558-4901 all 234 measured lines run.
- **Port fix (no behaviour change):** the window's last entry (ADD_ITEM, SET_POSITION) computed unsigned: a position
  near INT_MAX overflowed a signed int in the port (undefined; the compiled code wrapped as the original's ADD).
- **Original's flaws, port already fixed:** the row formats the entry's name and map as C_fstr's format (a server named
  "a%sb" shows stack garbage, or faults in the original's printf; the port's "%s"): the tests keep '%' out of them.
  The row's ADD_ENTRY without a box (the port checks it; the rows always have one: not tested).
- **Original's flaws kept (both sides, states the tests keep to):** a row whose Create failed (a frame or an icon
  missing from the model) keeps null icons, and the list's refresh shows an entry on it unchecked (both crash; the
  Create tests give complete rows, or a list without entries). A click on a row past the entries (the list's Select
  scrolls on for ever after RowClicked's refresh made the last shown the entries' last, part 56's endless Select): the
  game hides those rows; the click cases' lists show all their rows with an entry each.
- **Not tested:** a row's ADD_ENTRY without its box (above); '%' in names and maps (above).
- **Mutations:** 171 mutants, 9 not building, 151 of 162 killed, 11 equivalent (mutate_controls_server.py).
- **Harness lessons (my mistakes):** the first runs crashed without a report: the original's printf on a '%' name, and
  its _aligned_malloc on an unmade heap (a first-chance handler and the echo of the round found them); the Input case
  hung (bisected by rounds, the hung child's stack taken with gdb -p: Select); timed-out runs left their relaunched
  children running (the timeout killed only the parent: taskkill them after a hang); a heredoc mangled a script's
  backslashes again (Write for such scripts); the kit's stub made weak broke the ten other controls builds (MinGW's PE
  weak symbols don't resolve across objects: a macro now; build every harness of a shared kit).

## Human part 60 (the hot key and the titled edit box, `controls.cpp` lines 4902-5159, 2026-11-19)

- **The build:** `controls/build_hotkey.bat` (`hotkey_diff.cpp` with the kit). New in the kit: a kind's second owned
  control (S_Kind::ownedAt2 / ownedSlot2: the hot key owns its title, +0x74, and its edit box, +0x78; adopted, cleared and
  deleted as the first); the frame name "hktext*"; the stubs of PollPress, Bind, GetBindingName and the cursor's SetDelay
  left out by CONTROLS_KIT_HAS_HOT_KEY. The part's fakes, both sides: the input's PollPress (0x006c87f0 on 0x008aeb58: a
  round's press or none), the key bindings' Bind (0x006a8670) and GetBindingName (0x006ab840: its name through the
  hidden pointer) on one player profile block (the port's g_gameProfile.m_player, the original's 0x008aea2c; the
  bindings at +0xEC), the cursor's SetDelay (0x0066ab50).
- **The tests:** planned trees of hot keys and titled edit boxes, each with its edit box (id | 1) and title (id | 2) owned;
  armed or not, bindings in range and not; the round's press (Escape on the keyboard, its neighbours: another key or
  device, any), Bind's answer, the binding's name (empty, long, UTF-8, none). The kit's cases; the own messages straight
  to them (the bindings' get, get 2 and set with the title's text id, REFRESH; the titled box's text messages to its
  title, the edit ones to its edit box, the others to the base); the input (the slots with the edit box editing or not,
  Unknown20's keys with high bytes, the tick armed or not, the refresh, SetInactive, SetBinding +0x84); Create (the
  titles' frames found or not); the constructors' fields.
- **Results:** 10,564,418 checks, 0 failures; lines 4902-5159 all 141 measured lines run.
- **Original's flaws, port already fixed:** the factory never sets the binding (+0x70: whatever the block held until a
  SET_BINDING; the port's 0); the tick sets the cursor's delay without a cursor check (a crash without one; the port
  checks: the tests always have one).
- **Not tested:** the dedicated server's Unknown20 (HD2_DEDICATED, not built); a hot key without its edit box or title
  (both use them unchecked, both crash); Create on no frame (the base's crash, both).
- **Mutations:** 142 mutants, 1 not building, 137 of 141 killed, 4 equivalent (mutate_controls_hotkey.py).
- **Harness lessons (my mistakes):** the generator made only 34 mutants (the range's lines repeat elsewhere in the
  file): the other 108 written by hand with their neighbours, each checked unique before the run.

## Human part 61a (the item pictures, `controls.cpp` lines 5161-5296, 2026-11-19)

- **The build:** `controls/build_pictures.bat` (`pictures_diff.cpp` with the kit). The item table part 55's (a few
  definitions of each record type on both sides, the original's at 0x0098b030; their pictures cached).
- **The tests:** planned trees: item pictures under a plain control (the original tells the parent of the counts
  unchecked), each with its two pictures (id | 1, id | 2), now and then swapped or a child added. The kit's cases; the
  own messages straight to them (SET_ITEM with items of each record type, ammo, uniforms' headgear, ids out of the table;
  the counts below, at and above 0; the callbacks; REFRESH); the input slots, FindChild and Refresh shown or not; without
  the second picture the port alone (the original crashes): its own messages and Refresh, no call reaching the second;
  the constructor's fields. A texture named "X_SQUARE.BMP" in half the rounds (Create's face group found).
- **Results:** 11,337,094 checks, 0 failures; lines 5163-5165 and 5173-5296 all 83 measured lines run.
- **Original's flaws, port already fixed:** the second picture used unchecked by Refresh, the counts, the callbacks,
  REFRESH and SET_ITEM (an item pictures without "ammo*" crashes on nearly anything; the port checks).
- **Not tested:** Create on no frame (the original's `if (frame)` then the frame used anyway: both crash); the pictures
  under no parent (both tell it unchecked).
- **Mutations:** 159 mutants, 7 not building, 151 of 152 killed, 1 equivalent (mutate_controls_pictures.py).
- **Harness lessons (my mistakes):** no fake texture was named "X_SQUARE.BMP", so the face group was never found and two
  mutants survived (the name changed, the second picture's search sent to the first): one texture named so in half the
  rounds now (part 55's item information has the same blind spot: its generator skipped those lines). The first no-second
  check matched node names in hex values ("c3"): only the call's object word compared now. Scratch scripts written
  through Bash heredocs lost their backslashes twice: rewritten with the Write and Edit tools.

## Human part 61b (the players' list and the check group, `controls.cpp` lines 5166-5169, 5298-5420, 2026-11-19)

- **The build:** `controls/build_players.bat` (`players_diff.cpp` with the kit). New in the kit: the stubs of
  C_NetGame::LockPlayers and UnlockPlayers left out by CONTROLS_KIT_HAS_PLAYERS. The part's fakes, both sides: the
  network game's players (the port's g_netGame.m_players; the original's LockPlayers, 0x0072f040, answering an MSVC 6
  vector of the same 0xC4-byte blocks; UnlockPlayers, 0x0072f100): none to six, free (no name) or not, names with
  '%', UTF-8, 51 letters, stats of all sizes. The scrolled text, scroll bar, buttons and slider part 53's, the
  highlighted check box and its halves part 51's.
- **The tests:** planned trees: players' lists with their scrolled text (its list always, its bar mostly) and title,
  and a third child always (a plain control, a label, a title, a check box's half: the one kind refusing a leave);
  check groups with their highlighted check box or none, now and then another child. The kit's cases; the own messages
  (SHOW_PLAYERS often, the two ignored, the others; targets kept by the list or routed to its children); the input slots
  with the focus anywhere, FindChild, SetInactive, SetStrings; the constructors' fields. SHOW_PLAYERS counted: about 600
  runs in a quick run.
- **Results:** 16,678,068 checks, 0 failures; lines 5166-5169 and 5298-5420 all 79 measured lines run.
- **Original's flaws (both, not fixed):** the list's FindChild answers 2 for the title's id (its index is 1):
  FindControl uses the index unchecked, so with Create's two children only, finding the title's id reads past them (the
  tests give the list a third child; not changed: main stays faithful). SetInactive and SHOW_PLAYERS use the title
  unchecked (Create makes it only with "header*").
- **Not tested:** a players' list without its title or scrolled text (both crash); SHOW_PLAYERS' messages' unset words
  (the original's stack: the columns' value3, the rows' value2 and value3; the port's 0; no difference seen).
- **Mutations:** 139 mutants, 9 not building, 122 of 130 killed, 8 equivalent (mutate_controls_players.py).
- **Harness lessons (my mistakes):** the first tree plan crashed both sides in FindControl (the title's 2 past the
  children); SHOW_PLAYERS ran 47 times in a quick run at first (most targets went to a child): steered now. The first
  coverage run crashed with no report, run beside the mutation run (likely the cause; alone it passed). The generator's title substitution also
  rewrote PLAYERS_TITLE in a mutant: a placeholder now.

## Human part 62 (the movie control, `controls.cpp` lines 5422-5562, 2026-11-19)

- **The build:** `controls/build_movie.bat` (`movie_diff.cpp` with the kit). New in the kit: the stubs of
  I3DGetDriver and I3D_visual::UpdateWBoundProc left out by CONTROLS_KIT_HAS_MOVIE. The part's fakes, both sides with
  the same loggers (stdcall, this on the stack): one video stream object (ISND_stream: IsPlaying, Open, Close, Play,
  Stop, SetRect, GetVideoSize, GetState, SetVolume, UpdateVideo, Draw, Release; a round's answers) and a second for
  CreateStream's, the graph's CreateStream (0x38: a stream or none), SetRenderState and GetRenderState (0x74, 0x78), the
  driver's SetState and GetState (I3DGetDriver: the original's import 0x007e529c; the getter not logged: the port asks
  twice in Refresh, the original three times), the visual's UpdateWBoundProc (the original's import 0x007e5356).
- **The tests:** planned trees of movies (alone or under a plain control); the frames' world bounds now and then
  empty, flat, swapped or out of date. The kit's cases; the own messages straight to it (PLAY_MOVIE with names, empty,
  none, '%'; Open answering success or failure, the stream playing or not; STOP, CLOSE; the others); Refresh (playing or
  not, the stream's state finished, -1, others; the driver's and the graph's states), FindArea (also without a frame),
  FitRect (videos of every shape, of no width, height or size, negative; areas with zeros, infinities, NaNs),
  CloseStream (also without a stream) straight; the constructor's fields.
- **Results:** 5,078,272 checks, 0 failures; lines 5422-5562 all 81 measured lines run (the dedicated server's lines not
  built).
- **Port bug found and fixed (on main; a port mistake):** FitRect converted its floats to int in 32 bits: a video of no
  width or height (an infinity, a NaN) gave the stream's rectangle INT_MIN where the original's __ftol, truncating
  through 64 bits, gives 0 (the low word of the "indefinite" value). Now through 64 bits, as human_combat.cpp's percents.
- **Original's flaws, port already fixed:** the factory never sets the stream (+0x6C: whatever the block held until
  Create; deleting a movie that was never made would close and release it; the port's null).
- **Not tested:** the dedicated server's Create and HandleMessage (HD2_DEDICATED, not built); Refresh and the messages
  without a stream (both crash: Create failing leaves the control without one, used unchecked).
- **Mutations:** 150 mutants, 10 not building, 138 of 140 killed, 2 equivalent (mutate_controls_movie.py).
- **Harness lessons (my mistakes):** I read the first failure the wrong way round (port / original): the log's first
  half is the original's. The generator's range ran one line past the file's end.

## Human part 63 (the compass and its radar, `interface_compass.cpp`, 2026-11-19)

- **The build:** `interface/build_compass.bat` (`compass_diff.cpp`, which includes `interface_compass.cpp` to reach its
  statics; `interface_globals.cpp`: raw blocks for the team manager, the profile, the game role). The first suite of the
  interface (`_re/harness/interface`).
- **Fakes, both sides with the same loggers (stdcall, this on the stack):** an arena of frames (Release, SetWorldDir,
  Update, SetOn, LinkTo, FindChildFrame, Duplicate, SetAlpha; the original's UpdateWMatrixProc import), the scene's
  FindFrame, the driver's CreateVisual (the original's I3DGetDriver import), the humans' methods (IsSurrendered,
  IsUnconscious, GetFacingDir, GetViewDir, GetMoveMode, GetStateFlags, IsLeftFootForward: the original's redirected;
  role, health and frame at both sides' offsets in raw blocks: g++ lays C_Human out its own way), the active player, the
  difficulty, the game's time (the original's game pointer), RandFloat (a sequence), the CRT's rand (seeded the same), the
  original's new and delete. LS3DF's own AngleTo, S_quat::Make and Normalize on both sides.
- **The tests:** scenarios of up to 16 operations (Init, Show, Update, ShowSound, the radar's Add, Remove and Update,
  Done; between them the active soldier, the humans' answers, health, role and places, the game's time, difficulty and
  role, the compass's angles and speeds set straight) run from one start on each side, the logs of every call and the
  fields after each operation compared, the frames byte for byte at the end; no float tolerance needed. UpdateWobble
  straight; the statics as loaded; the port's ShowSound fix and Forget alone.
- **Results:** 11,494,590 checks, 0 failures; all 235 measured lines run.
- **Port bugs found and fixed (on main; port mistakes):**
  - UpdateWobble's tilt multiplied by the double 0.1; the original's constant (0x008104a8) is 0.1f widened
    (0.10000000149): the arrow one ulp off in most frames.
  - UpdateHeading subtracted the rounded angle from 2 pi; the original subtracts AngleTo's unrounded x87 result: now
    AngleToNorth computes LS3DF's AngleTo in double (its acos as the CRT's atan2 form, as ls3df/src/math does) and the
    difference rounds once (up to 4 ulps of the heading before).
  - A blip's radius summed z first; the original sums (x x + y y) + z z.
  - The magnitudes (the blip's radius, the heading's length) tested "below 1e-8" as a float compare; the original's
    inline test compares the bits as integers, so a NaN with its sign bit set counts as 0 (a NaN view: heading 0, the
    port's pi). A local MagnitudeOfSquare as LS3DF's.
  - ShowSound converted the direction to int in 32 bits: a NaN direction gave INT_MIN where the original's __ftol,
    truncating through 64 bits, gives 0. Undefined behaviour without a visible effect (INT_MIN rows of 8 bytes wrap to the
    first mark on x86: the hand mutant undoing it survives); now through 64 bits, as part 62's FitRect.
  - s_wobbleDecay started at 1; the original's is in .bss (0 until Init sets 1; nothing reads it before: no effect).
- **Original's flaws, port already fixed:** ShowSound's direction a hair west of north (+ 2 pi rounds to 2 pi) made a
  ninth direction: the original writes its 500 ms past the marks' times (over m_leftFoot, or past the compass for the
  red mark); the port clamps to 7 (checked on both: the original's write past the marks every time). The radar kept a
  deleted human's blips; the port's Forget drops them (tested alone).
- **Not tested:** NaN sign propagation (x87 and SSE pick different NaN signs: a NaN radius times a NaN bearing; the +NaN
  template left out); AngleBetween2D (the
  original's on both sides: the human movement's suite).
- **Mutations:** 466 mutants (6 by hand: the fixes undone), 28 not building, 402 of 438 killed, 36 equivalent (mutate_interface_compass.py).
- **Harness lessons (my mistakes):** I first took the port's 1-ulp misses for x87 precision until a direct UpdateWobble
  test showed every miss on one side: a constant, not precision. A full-length run found NaN-sign differences the quick
  runs never reached: run the full length after adding special inputs. Writing this section I first noted a NaN sound
  direction as crashing both sides: the original's __ftol makes it 0; checking that claim found the port's 32-bit conversion, which the address arithmetic then made harmless.

## Human part 64 (the message log and the chat line, `interface_messages.cpp`, 2026-11-20)

- **The build:** `interface/build_messages.bat` (`messages_diff.cpp`, which includes `interface_messages.cpp`). New: the
  interface's kit (`interface/interface_kit.inc`: checks, logs, an arena of frames with logged slots, the scene, the
  driver, the graph, the original's common redirects, the log and arena comparisons, the crash handlers, LS3DF's
  S_quat::Make and Normalize forwarded) for the parts from here on.
- **Fakes, both sides with the same loggers:** the font manager (Acquire, Release, FindFont, the manager's Release) and
  the fonts (each side its own object: g++ gives the virtual destructor two slots; Select logged, GetGlyph from the
  round's table), the graph's screen, DrawPrimitives (every vertex logged), GetAsciiKey, ReadChar, FlushChars, IsKeyDown
  (its key a byte: the original pushes a register with other bits above it), the driver's states, the area frame's
  bound (UpdateBound: a new one each call, following the frame's place), the console's line and command, ReadUnicodeInput,
  the game's SetPaused. The port's DecodeUtf8 and EncodeUtf8 are the original's (the text box's).
- **The tests:** scenarios of up to 24 operations (Init, Done, Update with the keyboard's answers, Render, AddMessage,
  SetMode, StartSay, StartSayTeam, OpenInput; the unicode flag, the game's role, the font found or not), with bursts: a
  1 to 4 byte character typed byte by byte then a unicode backspace, the line filled to its 255 bytes (one by one or at
  once) then backspaces, closed, opened, a backspace on the empty line. Colours and areas with NaNs, infinities, 1e10,
  edges exactly on 0 (-0 pixels) or on a whole pixel. No float tolerance needed.
- **Results:** 34,758,606 checks, 0 failures; all 343 measured lines run (the dedicated server's 3 lines not built).
- **Port bugs found and fixed (on main; port mistakes):**
  - The float to int conversions were 32 bits (a line's colour from AddMessage, the layout's pixels): a NaN or an out
    of range value gave INT_MIN where the original's __ftol gives the low word (NaN 0, 1e10 1410065408; the glyphs drawn
    from x = 0, not -2^31). One helper (Ftol) for all of them.
  - The caret: the original draws its quad without the blank test (the width straight to __ftol); the port's AddGlyph
    skipped a blank '_'. Now AddGlyphQuads, the quads whatever the width.
  - The blank's width: the original's double 0.4, not 0.4f widened (the same pixels: faithful only).
- **Original's flaws, port already fixed:** a backspace in the unicode mode on a line of only continuation bytes reads
  the byte before the buffer (the history rectangle's last float's top byte, never a lead byte: harmless); the port stops.
- **Not tested:** a log of 0 lines (Init's maxLines 0: both delete the front of none; the game passes 50); the dedicated
  server's lines; DecodeUtf8 and EncodeUtf8 (the original's on both sides: the text box's suite).
- **Mutations:** 589 mutants, 51 not building, 520 of 538 killed, 18 equivalent (mutate_interface_messages.py).
- **Harness lessons (my mistakes):** IsKeyDown's first difference was the original's register junk above a byte
  parameter, not a port bug. An edit dropped the scenario generator's closing brace and its tail (caught by the build). The
  long-input bursts were needed: random typing never reached the 255-byte limit (enter in almost every long string).

## Human part 65 (the squad members' panels, `interface_members.cpp`, 2026-11-20)

- **The build:** `interface/build_members.bat` (`members_diff.cpp`, which includes `interface_members.cpp`, with the kit).
- **Fakes, both sides with the same loggers:** the text boxes (the port's C_TextBox defined in the test; the original
  builds its own inline: operator new, the graph's screen, AcquireFontManager; its AttachTo, SetText, SetTextId, AddText,
  Clear, Render and deleting destructor redirected; boxes named by their first appearance, a deleted one giving its name
  up), the parts' meshes, levels, face groups (DuplicateMaterial) and materials (SetAmbientColor, SetTexture,
  ResolveTexture), the graph's LoadTexture, the humans' methods (GetProfileUnknown24, GetLastName, GetShortName,
  GetTeam, GetHandsWeapon, FindItem, GetStat, the health, stamina and disguise per cents, GetVisibility, GetOrderCount,
  the squad icons, IsLocalPlayer; role, health, frame, net id, containers, profile, inventory flag and scale at both
  sides' offsets in raw blocks: g++ lays C_Human out its own way), the uniform's container (GetItem: each side its own
  object, the port's slot 4 bytes on), the texts ("T" + the id), the rank names, the multiplayer settings, the profile's
  IsCooperativeGame, the game's GetVipSlot, the active player. C_fstr and printf each side's own (the original CRT's
  float formatting set up first: _cfltcvt_init).
- **The tests:** scenarios of up to 20 operations (Init, SetHuman, SetSelected, Update, Render, Relayout, Done; the
  humans' answers and fields, the active soldier, the game's role and type, the VIP slot, the co-operative flag, the
  uniform, the humans' places (at times exactly 40 m apart at a height), the panel shown or hidden); the panel's fields
  Init leaves alone given each scenario's start values on both sides. The death image failing to load only without Done.
- **Results:** 5,233,625 checks, 0 failures; all 245 measured lines run.
- **Port bugs found:** none.
- **Original's flaws, port already fixed:** "AImode*" may be missing (Init allows it) but Update and the death use it
  unchecked: a crash; the port checks (tested on the port alone: never asked for it). Init's photo colour copies a stack
  word into its w (nothing reads w: left out of the comparison). The inline SetScale copies a 16-byte temporary into the
  frame (+0xDC a stack word on both sides: put back before the comparison, as part 50's bars).
- **Not tested:** the death image failing with Done (both release it unchecked); the dedicated server's line.
- **Mutations:** 446 mutants, 23 not building, 420 of 423 killed, 3 equivalent (mutate_interface_members.py; the
  last full run, then a health of 1 added to the answers' picks: G 261 num killed in the survivors' rerun).
- **Harness lessons (my mistakes):** the original CRT's float formatting crashes until _cfltcvt_init runs (part 57's
  lesson, forgotten: a hang under ntdll's heap lock); the earshot's edge needed a placed scenario (random places never
  made 1600 exactly); the coverage build kept a g_texts reference the optimised one dropped (raw storage added).

## Human part 66 (the status panels and the crosshair, `interface_status.cpp`, 2026-11-20)

- **The builds:** `interface/build_status.bat` (66a: `status_diff.cpp`: the speedometer, the reticles, the weapon panel,
  the stance panel, the progress bar, the helpers FindTexture and DuplicateMaterial) and `interface/build_crosshair.bat`
  (66b: `crosshair_diff.cpp`), both including `interface_status.cpp`. New: `interface/interface_gfx.inc`, the text boxes
  (the port's C_TextBox; the original's built inline, its methods redirected; AddTextId added) and the graphics (meshes,
  levels, face groups, materials with all their colour slots, textures with names and reference counts, the frames'
  DuplicateMesh and SetLightLevel) for the parts from here on.
- **Fakes:** the humans' item and stance getters, GetActionProgress, the items (GetAmount), the definitions (GetCarryMode,
  GetBehaviour, GetAmmoItemId, GetImage), the vehicle (the seat weapon's four virtuals), the actors (GetName,
  GetSeatOccupant, GetSeatCount; type, vehicle, carried body, health, role at both sides' offsets), IsFriendOf (the
  original's IsHostileTo run as it is), GetFullName, GetProfileUnknown24, GetStat, GetHealthPercent, GetAccuracy, the car's
  HasEnemyAboard, the team's IsMember and IndexOf, the texts, the rank names, IsCrosshairAllowed. Objects with virtuals are
  shared blocks given each side's vtable before its run (the port's slots 4 bytes on: g++'s second destructor slot).
- **The tests:** a case per panel (66a: up to 16 operations with the world changing between them; the helpers straight on
  random meshes) and scenarios of up to 20 crosshair operations (Init, Done, Show, SetTarget on every kind of actor,
  SetColor, Update, Render); the panels start from the same random words on both sides (Init must set every field), the
  boxes' margins logged after Init. Amounts NaN, infinite, 1e10, 2^31.
- **Results:** 10,736,917 checks (66a) and 5,002,777 (66b), 0 failures; all measured lines run (66a 282 of 282
  with SetPosX / SetPosY in 66b's; 66b 168 of 168).
- **Port bugs found and fixed (on main; port mistakes):**
  - The weapon panel's rounds (a float amount) converted in 32 bits: -1e10, NaN or 2^31 gave INT_MIN where the
    original's __ftol gives the low word. Through 64 bits now (Ftol).
  - The crosshair's Init asked the mesh for the level once per part; the original asks again (GetLevel, GetFaceGroup)
    before each of the material's five calls. The same calls now.
  - The crosshair's player name fetched the rank's text before the first aid test; the original fetches it after, in
    each branch. The same order now.
- **Original's weaknesses (shared, the port's the same):** FindTexture tests neither the face groups' materials nor their
  textures; the weapon panel's Init uses the "X_SQUARE.BMP" texture unchecked (and a second Init after an item's picture
  replaced it would find none: the game inits once); the reticle's Init checks the binoculars' frame, then switches it off
  unchecked (a quirk kept); SetColor's and SetScale's 16-byte copies carry a stack word (the colour's w, the frame's +0xDC:
  left out of the comparisons).
- **Not tested:** the reticles missing with SetMode (both crash); the scene's "scope" or "sniper" missing (both crash).
- **Mutations:** 66a 497 mutants, 29 not building, 461 of 468 killed, 7 equivalent (mutate_interface_status.py); 66b 217,
  32 not building, 182 of 185 killed, 3 equivalent (mutate_interface_crosshair.py).
- **Harness lessons (my mistakes):** the item blocks were not restored between the sides at first (an item's definition
  changed by the original's run leaked into the port's); the icons' face groups shared materials, so a later group
  overwrote the "X_SQUARE.BMP" texture; Init's resets survived until the panels started from random words; I authored a
  backslash through a Bash heredoc again (a printf's "\n" became a line break; fixed with Edit).

## Human part 67 (the overlays, `interface_overlays.cpp`, 2026-11-20)

- **The build:** `interface/build_overlays.bat` (`overlays_diff.cpp`, including `interface_overlays.cpp`; linking
  `common/radix_sort.cpp` for the score table's sort). The kit's frame arena is now sized per harness (INTERFACE_FRAMES).
- **Fakes:** the video streams (the graph's CreateStream; Open's answers, Release, Close, Play, Stop, SetPosition,
  GetVideoTexture, UpdateVideo), the CRT's rand, the sound module's Play (every field of the request logged), the effects'
  SetFadeLevel (the original's slot 0x88, the port's from its member pointer), the scene's camera, the game (scene, round
  and respawn time left), the texts, the actors module (its actors vector at the original's address, the carnage count),
  the net game (team scores, the players' states and their lock), the local player's slot, the game role, the settings,
  the cutscene flag, the humans' IsSpectating and GetTeam. The world is written to both sides' memory before each
  operation (the port's objects, the original's addresses).
- **The tests:** a case per overlay (Injury, BigMessage, Objectives, Voting, Photo, Carnage, Pause, Countdown, Score,
  Timers): scenarios of its operations with the world changing between them, from the same random start words on both
  sides; the boxes' margins logged right after Init and the timers' SetPos; the fields snapped after each operation, the
  frames and graphics compared at the end. Scores and times NaN, infinite, 1e10, 2^31.
- **Results:** 26,300,268 checks, 0 failures; all 369 measured lines of `interface_overlays.cpp` run (`radix_sort.cpp` 61 of
  64: the sort's other key widths).
- **Port bugs found and fixed (on main; port mistakes):**
  - The score table's team scores (floats) converted in 32 bits: NaN, 1e10 or 2^31 gave INT_MIN where the original's
    __ftol gives the low word. Through 64 bits now (Ftol).
  - The photo's shutter sound took the camera's world position (x, y, z) and left the request's w its default; the
    original copies the matrix's whole fourth row, w too. The same 16 bytes now.
- **Original's weaknesses:** the occupation score reads 32 actor slots whatever the actors vector's count (the port, from
  its translation, stops at the count; the harness's slots past the count are null, where both count the same).
- **Not tested:** the score table's actors other than humans and explosive places (the harness's actor blocks overlap
  between the two types' fields, so the score case uses humans only); a video failing to open on one side only.
- **Mutations:** 549 mutants, 42 not building, 496 of 507 killed, 11 equivalent (mutate_interface_overlays.py).
- **Harness lessons (my mistakes):** the original's settings pointer was left unset at first (the original read null);
  a human's and an explosive place's fields overlap in the shared actor blocks (now type-specific writes); a static_assert
  on a port size that g++ pads differently; I authored backslashes through a Bash heredoc again (the build file's
  "\\radix_sort" became a carriage return; the file rewritten with Write).

## Human part 68 (the pickers, `interface_selectors.cpp`, 2026-11-20)

- **The build:** `interface/build_selectors.bat` (`selectors_diff.cpp`, including `interface_selectors.cpp`). The kit's
  graphics can keep materials out of the duplicates' pool (g_materialPoolEnd) and log a box's margin on Render
  (g_logMargins).
- **Fakes:** the humans' orders (raw blocks per side: the port's S_HumanOrder has no vtable word and its own layout), the
  scene's PickFrame and TransformPoints, the graph's screen size; for the weapon selector the driver's visuals (a pool, an
  "i_frehnd.bmp" face group each, released ones reused and their scales logged), the team's selected member and active
  player, the containers (per-side vtables; the original's GetSlots an array of pointers to MSVC vectors, the port's of
  std::vector), the items and definitions, the humans' breath, order, ammo and item counts, RequestMoveItem,
  RequestTransferItem, IssueCommand, the item images.
- **The tests:** a case per class (Info, Cursor, Weapons) of up to 16 or 20 operations with the world changing between
  them, from the same random start words on both sides; the cursor's motions also landing exactly on the screen's and the
  clip rectangle's bounds, IsInRect's rectangles with an edge on the cursor, the weapon keys held through whole scenarios
  and a scripted sequence running both keys' repeats (both orders, then a choice), times adding up to the 2 s idle limit, amounts NaN,
  infinite, 1e10, 2^31. A port-only case (QueueFixes) on queues of more than 10 orders and orders without icons.
- **Results:** 25,128,284 checks, 0 failures; all 398 measured lines of `interface_selectors.cpp` run.
- **Port bugs found and fixed (on main; port mistakes):**
  - The cursor's screen position and the weapon list's rounds (floats) converted in 32 bits: NaN, 1e10 or 2^31 gave
    INT_MIN where the original's __ftol gives the low word. Through 64 bits now (Ftol).
- **Original's weaknesses:** a queue of more than 10 orders runs the original past its icons, and an order without an icon
  of its own shows the previous order's (for the first one an unset stack word): the port's fixes (from its translation)
  are tested alone; Show(true) while shown builds the weapon list again after the old one (the entries doubled until Done;
  shared; the harness shows at most 4 times per scenario); HandleInput and Update use the human unchecked when no list was shown (shared; not run); the cursor's
  Init copies a stack word into the positions' w (set to the port's 0 after Init).
- **Not tested:** the weapon selector without a human (both crash); a template entry's visual without the "i_frehnd.bmp"
  face group (both crash).
- **Mutations:** 697 mutants, 69 not building, 611 of 628 killed, 17 equivalent
  (mutate_interface_selectors.py).
- **Harness lessons (my mistakes):** stubs declared without their bool returns; the order blocks shared between the sides
  at first (layouts differ); the cursor's pick and transform leaked from one side's run into the other's; the port's
  vector at +0x14 was overwritten by start words; the visuals' pool ran out (repeated Show(true) doubles the list) and the
  duplicates' pool overwrote the visuals' "i_frehnd.bmp" materials, crashing the original once in 20,000 scenarios; the
  first mutation run left 25 killable survivors (the visuals' scales, the entries' margins, RequestMoveItem's slot, the
  held keys, the bounds, the idle limit, the marker past the last icon): inputs and logs added; the next two full runs
  each let a few of the earlier kills through (the new inputs moved the random streams: rectangles' edges on the cursor,
  key repeats reaching 0 and 1 ms, the previous key firing again, a medkit chosen while carried had been luck), so they
  are now made on purpose (rectangles around the cursor, a scripted key sequence in both orders ending in a choice).

## Human part 69 (the mission information and the scores, `interface_info.cpp`, 2026-11-20)

- **The build:** `interface/build_info.bat` (`info_diff.cpp`, including `interface_info.cpp`; common/chunk.cpp linked: the
  original's inline chunk code runs on the port's C_chunk, the same layout). The kit's net game storage grew to 0x8000
  (the server's name at +0x4E00); `mutrun.py` copies rw_data's include directory into its trees.
- **Fakes:** the driver's CreateFrame and states (up to 0x3F), the model's SetName, the scene's AddFrame and primary
  sector, the model cache (the port's OpenInto; the original's inline lookup answered by FUN_00409660's slot and the
  cache's slot 0x30), the text styles (every word logged: the 73 styles' table compared), the graph's render states, the
  texts (kept alive: the original reads a hint's characters after releasing its copy), ReplaceTags (three tags), the
  campaign's mission, the missions and objectives (per side), the game, the settings, the net game (the players,
  scores, wins, server's name), IsSpectating, the humans' health and uniform (the original's inline path), the items'
  teams, rw_data's files (in memory).
- **The tests:** scenarios of up to 20 operations (Init, Done, Update, Render, Show, IsShown, AddHint (hints before Init
  too), ClearHints, SaveHints, LoadHints) with the world changing between them (players, game, settings, mission, the
  frame, a player's spectating and health), from the same random start words on both sides: every game type, single
  player and multiplayer roles, up to 24 players (over 16 with distinct names and scores), equal scores and names
  differing in case, ranks out of range, scores NaN, infinite, 1e10, 2^31, hints with tags known and not and '<', '>'
  in every order. A port-only case (InfoFixes): Init without the model or its "game_info", LoadHints with part of an
  id at a chunk's end.
- **Results:** 5,127,170 checks, 0 failures; all 365 measured lines of `interface_info.cpp` run.
- **Port bugs found and fixed (on main; port mistakes):**
  - The teams' scores (floats) converted in 32 bits: NaN, 1e10 or 2^31 gave INT_MIN where the original's __ftol gives
    the low word. Through 64 bits now (Ftol).
  - The team columns put a blank line after every allied player; the original's only in place of an empty left row
    (the axis' player follows on the same line).
- **Original's weaknesses:** Init goes on without the model or its "game_info" (a crash; the port returns false: tested
  alone); LoadHints loops forever on part of an id at a chunk's end (the port skips it: tested alone); the original
  reads a hint's characters after releasing the text's copy (harmless while the texts table holds it); it fetches text
  0xA40 and does not use it (left out of its log); past 16 players its sort is not stable (equal scores or names may
  come in another order than the port's: not compared).
- **Not tested:** the original's model cache when "ingame_info" is not cached yet (its file loading: the cached path
  only); a malformed chunk (sizes past their parent).
- **Mutations:** 647 mutants, 104 not building, 529 of 543 killed, 14 equivalent
  (mutate_interface_info.py).
- **Harness lessons (my mistakes):** the mutation trees lacked rw_data's include directory (the first run built
  nothing); GetLocalTeam logged although the sides call it different times (a pure read: not logged now); the first
  run left 3 killable survivors (Init's clearing of hints: none before Init; a hint with '>' but no '<'; SaveHints'
  Ascend: the chunk's depth not logged): inputs and a log added.

## Human part 70 (the signals panel, `interface_signals.cpp`, 2026-11-20)

- **The build:** `interface/build_signals.bat` (`signals_diff.cpp`, including `interface_signals.cpp`). The kit's globals
  gained raw storage for the controls, the collision world and the items module (the unoptimised coverage build calls
  their members on them).
- **Fakes:** each side its own 12 objects of any actor type (the port's actors are laid out their own way: the type at
  +0x0C, the original's at +0x18; the humans' vehicle, seat, carried body, health; the doors', radios', supply boxes'
  (the lists in both vectors' layouts), places' and animations' fields), per-side vtables (GetName, HasSeatWeapon, the
  weapons' GetAmmoItemId, the commander's CollectGroup and spacing; the port's slots from its member pointers); the
  team, the humans' queries and PlayerSendActionCommand, the cars' CanEnter / HasEnemyAboard / IsDestroyed, the
  bodies' collision, HighlightMembers, CanAddItemToInventory, the items' definitions (the original's table), the
  controls, the graph's keys, the camera module, the ray test, the click sound (a frame), the game role and type.
- **The tests:** scenarios of up to 24 operations (Init, Done, Open, IsOpen, SetEntry, HasTargetCommands, BuildPage,
  BuildTargetPage, Refresh, Activate, SelectNext, SelectPrevious, HandleInput, Issue, Render, SetTarget, Update,
  Relayout, OnTargetDeleted, AimPoint; pages and entries chosen) with the world changing between them, from the same
  start words on both sides: single player and multiplayer, every target type, cars with members aboard (or in another
  vehicle), supply boxes with lists or ammunition at the box's most, keys digits and others (high bits set), the roll
  button, the wheel both ways. Operations naming a target page without a target (both crash) are left out, as are
  SelectPrevious from -1 and OnTargetDeleted keeping the target (the original's target cleared first: the port's fix).
  A port-only case (SignalsFixes): a supply box of more than ten items, a member's name with '%', SelectPrevious from
  -1, OnTargetDeleted on the target (open and closed).
- **Results:** 3,562,249 checks, 0 failures; 484 of the 485 measured lines of `interface_signals.cpp` run (BuildPage's
  multiplayer check of the radios inside the single player branch cannot run; the original's is dead code too).
- **Port bug found and fixed (on main; a port mistake):** the unlocked door's lock entry used the selected soldier
  fetched at the start; the original asks the team for it again first (the same soldier: a call less, now made).
- **Original's weaknesses (the port's fixes, tested alone):** a supply box of more than ten items wrote past the
  entries; a member's name was used as the format; SelectPrevious from nothing chosen walked back through memory;
  OnTargetDeleted kept the freed target. Also: the command's dir.w is a stack word or the word after the view direction
  (not sent); a target page without a target crashes (reached only through entries the menus never make).
- **Not tested:** Init's text box's inline construction beyond the kit's (the same as earlier parts).
- **Mutations:** 886 mutants, 151 not building, 728 of 735 killed, 7 equivalent
  (mutate_interface_signals.py).
- **Harness lessons (my mistakes):** the port's CollectGroup is virtual (its slot first left to the "other" fake); the
  first run crashed silently in the original under its exception frame (a target page reached from start-word entries:
  found with the first-chance handler); the first mutation run left 25 killable survivors (Init's clearing of the
  target, member and selection: no start words there; the target pages rarely reached with their actors' states):
  start words, a focus object and a target-page script added; the next full run left 15 more that depended on the
  random draws (a body a member carries, explosives not planted with a member, the combat page's fire launcher): the
  focus and the script widened, the last full run left only the 7 equivalents.

## Human part 71 (the map screen, `interface_map.cpp`, 2026-11-20)

- **The build:** `interface/build_map.bat` (`map_diff.cpp`, including `interface_map.cpp`; 96 arena frames). The kit's
  globals' items module grew to 0x1000 bytes (the port's respawning items are at +0x7F8).
- **Fakes:** each side its own 12 humans (frame, net id, health, chat line and time), 6 items (their owners), up to 18
  map actors (mostly zones: type, objective, side, net id, a named frame), the game (mission name, scene, mission), the
  mission (its objectives' vector) and the squad (its soldiers' vector; the soldiers' spawn zones read back after each
  operation); shared: the objectives, the zones' frames, the scenes (the interface's: primary sector, camera with its
  range; the game's camera), the play mode, the settings. Logged on both sides: the team (GetActivePlayer,
  GetMemberCount, GetMember, GetSeenLivingEnemies), GetHuman, the humans' GetViewDir / IsUnconscious / IsInPlay /
  IsSpectating / GetTeam, GetVipSlot, the items' GetFrame, the cursor's SetMode / GetHoveredId, ClearAction,
  GetLocalTeam, the text boxes' SetRect, the driver's CreateFrame / GetState / SetState, the model's Open; the scene's
  templates and the parts the map's frames find (a hash of the frame and the name, some missing). The original's chat
  rectangle statics set beforehand (their first call registers an atexit in the mapped CRT).
- **The tests:** scenarios of Init then up to 16 operations (Show, Update, Render, HandleInput, SelectSpawnZone,
  ShowSelectedZone; the world changing between them: humans, team, VIP, items, zones' sides, objectives' statuses,
  squad, input, frames moved) and now and then Done, from the same start words on both sides (the kind, the count, the
  frame, the map, the selection marker, the markers): single player, deathmatch, occupation, objectives, cooperative
  and unknown game types, the model failing to open, no "center", no "map*" (where nothing is put on it), no
  "selectzone", no "allied" template, the user properties' scales (atof's corners), more than 15 zones, the views' and
  the camera's degenerate directions, a depth of exactly 0. Left out (both crash): Done with a marker never made, Show /
  Update / ShowSelectedZone without the selection marker in a team game, Update without an active soldier, single
  player with more members than markers (the port's fix). The original's SetScale writes m_scaleW from a stack word:
  not compared. A port-only case (MapFixes): a zone without "zone_disabled*", an objective without its parts, single
  player without an active soldier and with more members than markers.
- **Results:** 3528663 checks, 0 failures; 603 of the 604 measured lines of `interface_map.cpp` run (UpdateTeams' marker
  without a visual: only after Done, where both sides crash on the cleared selection marker first).
- **Port bugs found and fixed (on main; port mistakes, all orders of calls):** UpdateTeams asked the active soldier's
  side before the player's (the original the player's first); UpdateVip asked for the VIP slot before the active
  soldier; an objective's Update did not ask for the local side again before comparing it.
- **Original's weaknesses (the port's fixes, tested alone):** a zone's "zone_disabled*" written unchecked; single player
  read the active soldier's frame without one; more squad members than markers went past the markers; a multiplayer
  objective without its parts was kept half set up (its later Update and Done on null parts and a stale text box).
  Shared, not fixed: a team game without "selectzone" crashes in Show and Update; "map*" is read unchecked (Init's
  zones and objectives, Show); the VIP's slot is read unchecked; SetScale's m_scaleW is a stack word.
- **Not tested:** the text boxes' inline construction beyond the kit's (the same as earlier parts).
- **Mutations:** 1064 mutants, 162 not building, 852 of 902 killed, 50 equivalent
  (mutate_interface_map.py).
- **Harness lessons (my mistakes):** the port-only case first inherited a world without "map*" (a crash); the first
  mutation run left 11 killable survivors (no start words in the frame, the map and the markers; no exact bounds for
  the depth and the squares): added.

## Human part 72 (the roll menu, `interface_rollmenu.cpp`, 2026-11-20)

- **The build:** `interface/build_rollmenu.bat` (`rollmenu_diff.cpp`, including `interface_rollmenu.cpp`).
- **Fakes:** each side its own 12 objects of any actor type (the humans' health, vehicle, seat, carried body, role, net
  id; the doors', radios', supply boxes' (both lists in both vectors' layouts, state, flags), static weapons', item
  actors', places' and animations' fields), the hands weapons (+0x5C the ready time) and their definitions, the game
  (the mission time); per-side vtables (GetName, GetSeatOccupant, GetSeatCount, GetSeatText, GetAmmoItemId; the port's
  slots from its member pointers). Logged on both sides: the team (GetSelectedMember, GetCommandedMember, IsMember), the
  humans' queries, IssueCommand, PlayerSendActionCommand, RequestTakeFromBox, RequestPickUpItem, the bodies' climb and
  work states, the cars, the radios' CanUse, IsCooperativeGame, the texts' Get, GetActorById, CanPutItem, ShowInventory,
  the controls, the graph's keys, the visuals' UpdateWBoundProc (the bound: S_vector4s at +0x170 / +0x180), the click
  sound. Seats hold humans only; the team stays human.
- **The tests:** scenarios of Init (the crosshair's menu or the command map's) then up to 20 operations (SetTarget,
  HasActions, Open, Prepare, HandleInput, Update, Render, Relayout, IsCursorOver, Select, SelectAt, MoveTo, Refresh,
  BuildEntries, HasCommandMapActions, SetEntry, Activate, SelectPrevious, SelectNext; the target and the choice set
  directly) with the world changing between them, mostly aimed at a focus actor of every type and opened or built,
  an entry chosen and acted on; the selected soldier mostly free to act (else busy, in a vehicle, carrying a body);
  distances at the reach's edges and NaN, points on the bound's edges and far off. Left out (both crash or read past):
  a seat entry's occupant without a target, the actions on a null target or by nobody, the command map's entries
  without a target or a soldier, IsCursorOver without "kurzor", SelectPrevious from -1 and SelectAt's lines past the
  entries (the port's fixes). The original's Done leaves its freed text box in the menu (cleared after it, as the port
  does); the heal request's dir.w (a stack word) is not compared. A port-only case (RollMenuFixes): a supply box of 14
  items and a car of 12 seats, a player's name with '%', Activate without a choice, Select and SelectAt past the
  entries.
- **Results:** 5477197 checks, 0 failures; 625 of the 627 measured lines of `interface_rollmenu.cpp` run (the member's
  entries while the soldier carries a body: his body's branch comes first; the original's dead code too).
- **Port bugs found and fixed (on main; port mistakes):** the body's pick-up test (busy, stance, water) asked for
  every human target (the original only for a body); HasActions asked the target's side before the soldier's; the
  seats' occupants not asked for again for their names (the network game's seats, the Junkers'); the weapon's
  ammunition type not asked for again when 0; a NaN distance counted as out of reach (the original's x87 tests take it
  as within: SetTarget, its reopening, HasActions).
- **Original's weaknesses (the port's fixes, tested alone):** a supply box of more than ten items or a vehicle of more
  than nine seats wrote over the choice and the target; a player's name was used as the format; Activate without a
  choice read the field before the entries; Select and SelectAt read past the entries; SelectPrevious from -1 walked
  back through the fields before them (now noted in the port). Also: Done leaves the freed text box in the menu.
- **Not tested:** Init's text box's inline construction beyond the kit's (the same as earlier parts).
- **Mutations:** 1269 mutants, 261 not building, 994 of 1008 killed, 14 equivalent
  (mutate_interface_rollmenu.py).
- **Harness lessons (my mistakes):** the menu's bound first written as six packed floats (the engine's I3D_bbox has a
  w after each corner: the max at +0x180); SetTarget left out whenever a start entry was a seat (it refreshes only
  after building: most scenarios lost); the seats' occupants turned into humans (nearly every object became one, the
  targets with them; now a seat of another actor is emptied); a vehicle drawn for one soldier could be the other (the
  team made human again); found by the per-line counts and a crash under the first-chance handler.

## Human part 73a (the inventory's scroll bars, `interface_inventory.cpp` lines 98-520, 2026-11-20)

- **The build:** `interface/build_scrollbar.bat` (`scrollbar_diff.cpp`, including `interface_inventory.cpp`; the
  screen's outside references stubbed to abort: not run here).
- **Fakes:** the kit's frames (the bar, its arrows and slider, their looks: found by name, some missing; the bounds'
  UpdateBound); the owner's OnScroll patched on both sides to one logger (the original's 0x004a2590 redirected, the
  port's C_InventoryScreen::OnScroll overwritten with a jump); the original's floor redirected to the harness's (its
  CRT never started: an infinity's errno would go through a null heap). The original's bar, arrows and slider have
  vtables at +0 (their fields 4 bytes above the port's): each side snapped by its own layout.
- **The tests:** scenarios of the constructor, Init (the bar's, an arrow's, the slider's), then up to 16 operations
  (SetCount, Hover, Unhover, Press, Drag, Release, SetDisabled, Step, OnSliderMoved; the arrows' Show, SetDisabled,
  Activate, Deactivate, Press; the slider's Layout, Show, SetDisabled, Activate, Deactivate, SetPosition, Follow,
  SetPositionAt, Drag; frames moved and their flags flipped) and now and then the destructor, from the same start on
  both sides: counts from INT_MIN + 1 to a million, positions past both ends, NaN and the infinities, flat bounds (a
  zero range), sliders larger than their bases. Left out (both crash): the bar's Init with a part or a look missing
  (its SetDisabled shows the null looks), the parts' operations without their looks. After the destructor the
  original clears its parts' pointers (not compared). A port-only case (ScrollBarFixes): a second Init's range.
- **Results:** 6581125 checks, 0 failures; 249 of the 253 measured lines in the range run (GetFrameId: the screen's;
  Step's clamps after its guards: they cannot fire).
- **Port bug found and fixed (on main; a port mistake):** OnSliderMoved converted floor's value with a 32-bit cast:
  an infinite or NaN position (a slider over a base without height) gave INT_MIN, past the unsigned clamp, and
  scrolled to it; the original's __ftol gives 0 (the 64-bit result's low word). Now FtolLow, as the original.
- **Original's weaknesses:** the bar's Init does not check its parts (a missing one crashes in SetDisabled; shared by
  the port, not fixed); a second Init leaks the slider's range (the port frees it; tested alone).
- **Mutations:** 555 mutants, 32 not building, 485 of 523 killed, 38 survived: 5 in the screen's
  helpers (part 73b), the rest equivalent (mutate_interface_scrollbar.py).
- **Harness lessons (my mistakes):** the original's floor of an infinity set errno through its unstarted CRT and broke
  the heap (seen only as crashes in ntdll at later rounds; my memory notes had the cause); the first run's "a part
  missing" scenarios all crashed in Init (now left out, the parts' own Inits tested instead).

## Human part 73b (the inventory screen, `interface_inventory.cpp` lines 530-1357, 2026-11-20)

- **The build:** `interface/build_inventory.bat` (`inventory_diff.cpp`, including `interface_inventory.cpp`; 128 arena
  frames; its scroll bars run as they are, part 73a).
- **Fakes:** each side's screen built once (the original's constructor 0x0049fef0, the port's) and its bytes restored
  before each scenario (the original's text boxes are 0x9C bytes, the port's 0x84; its scroll bars 0x94 and 0x84: the
  screens snapped by each side's layout, the boxes by name, margin and dimming). Each side its own 4 humans (type,
  health, role, the containers' array, the carried weight, the inventory's flag, +0x1A5; GetName and GetHitPoints
  virtuals), their containers (GetMaxSpace, GetTotalSpace, GetUsedSlotCount, GetSlots: the original's an array of
  pointers to MSVC vectors, the port's libstdc++ vectors; GetItem), 16 items (definition, kind; GetAmount,
  GetMaxAmount), 8 definitions (text, weight, components; GetBehaviour, GetAmmoItemId, GetImage) and their components
  (kind, aim mode, aimed field of view; GetMuzzleVelocity). Logged on both sides: the humans' GetFullName,
  GetProfileUnknown24, IsUnconscious, GetHealthPercent, GetStat, GetStatFactor, GetViewDir and the item requests, the
  cursor (GetHoveredId, GetMove: the original's returns its argument, its caller passes that on; IsInRect; GetPos read,
  not logged: the port's is inline), the texts' Get, FindTexture and DuplicateMaterial (__fastcall in the original),
  the scene's PickFrame and AddFrame, the frames' SetName, the meshes' DuplicateMaterials; the rank names' texts (the
  original's table 0x008ae960). The pages' children found by a hash of the frame and the name.
- **The tests:** scenarios of Init, Open (the soldier, a body or another actor, modes 1 / 3 and others) and Show, then
  scripts (a press, holds and a release or a double click; the cursor resting on an icon for the description; a scroll
  bar hovered and clicked or dragged; a drag between the pages to a place, the drop place or none) and up to 14 other
  operations (Open, Show, Refresh, Update, Render, HandleInput, ShowSlots, StartDrag, SendAcross, Drop, OnPress,
  OnHold, OnRelease, OnDoubleClick, UpdateDescription, UpdateSkills, IsCursorInside, HideSelections, HandleScrollBars,
  IsShown; the world changing between them), from the same start words on both sides. Left out (both crash): the
  operations without a soldier, the right page without its actor, a drop by nobody, Init twice. The original's
  SetScale writes m_scaleW from a stack word (not compared). The original's copy protection in DragSlot (single
  player from campaign mission 14 on; dropped by the port) is not reached: the game's mission index stays below 14.
- **Results:** 7673048 checks, 0 failures; all 559 measured lines of the range (the file's helpers and the screen) run.
- **Port bugs found and fixed (on main; port mistakes):** ShowSlots asked a container for its item twice (the
  original three times: the test, the slot, the picture); Init's dragged icon took the slot icon's world direction
  normalised as x² + y² + z² (the original's inline copy sums (y² + z²) + x²: an ulp apart); the description's rounds
  asked for GetAmount before GetMaxAmount and the text after them (the original: the text, the most, the rounds).
- **Original's weaknesses:** none new (the shared crashes above are reached only through states the interface does not
  make).
- **Not tested:** the copy protection (above); the screen's destructor (each side's screen is built once).
- **Mutations:** 1297 mutants, 100 not building, 1177 of 1197 killed, 20 equivalent
  (mutate_interface_inventory.py).
- **Harness lessons (my mistakes):** the original's GetMove returns its argument and its caller passes that on (my fake
  returned nothing: the slider's Follow read a garbage motion and crashed once the scroll scripts ran); the first runs
  reached the description's end 21 times in 10,000 scenarios (the picked frame now mostly an icon's, as the pages find
  them).

## Human part 74a (the command map's set-up, opening, hints and buttons, `interface_command.cpp` lines 373-935, 2026-11-20)

- **The build:** `interface/build_command.bat` (`command_diff.cpp`, including `interface_command.cpp`; 128 arena
  frames). The file's 74b / 74c functions it calls (UpdateMember, PickTarget, ShowRollMenu, AddOrder,
  RemoveSelectedOrder, ApplyFlags) are patched on both sides to the same loggers (the original's redirected, the
  port's overwritten with jumps), so the build turns off what would optimise the callers against the real bodies:
  -fno-inline-functions-called-once, -fno-ipa-modref, -fno-ipa-pure-const, -fno-ipa-reference(-addressable); the
  mutation runner gets them through MUT_DEFS. The rest of the file's references are stubs that exit (not
  [[noreturn]]).
- **Fakes:** each side's panel built once (the original's hint boxes by its text box constructor 0x00496cc0, the
  port's panel by its constructor) and restored before each scenario; snapped by each side's layout (its text boxes
  0x9C / 0x84 bytes; the member lines by their counts). Each side its own 8 humans (type, frame, health, role) with 4
  orders each (kind, data, place, actor, flags, marker), the game (its scene, checkpoints, paused flag and state in
  place; SetPaused), the camera module (every slot of the original's gives the human effects); the team's commanded
  index and the enemy records' tactical flag written in place (the original's team in its own memory). Logged on both
  sides: the team (GetActivePlayer, GetCommandedMember, GetMember, GetMemberCount, IsMember, GetSeenEnemies,
  RefreshSight), the humans (GetOrderCount, GetOrder, GetStateFlags, GetAIModeFlags, GetStanceFlags, IsAISuspended,
  IsUnconscious, FadeOut, FadeIn, CancelOrders, the body's Unknown5ac470), the human effects (SetTactical,
  SetTacticalMode, UpdateTactical, SetTacticalZoom), the effects module's SetThunderstorm and Update (the port's slots
  the original's + 4), the profile's SetControlMode, the model cache's OpenInto, the info selector, the cursor's
  GetHoveredId (its mode, position and frame read in place), HighlightMembers, the roll menu (Init, Done, SelectAt,
  Update; its open flag from the world), the click sound; the scene's frames by a hash of the name.
- **The tests:** scenarios of Init and mostly Open, then up to 18 operations (Open, IsOpen, Update, ShowHint,
  UpdateHint, ReadOrderFlags, DarkenButtons, LightOrderButtons, LightFlagButtons, Press, SetFlag, PressToolButton,
  PressOrderButton, CycleZoom, S_Button::Light; the input's state set as 74c's handlers leave it; the world changing)
  and now and then Done, from the same start words on both sides (what Init clears or leaves; spare markers for
  Done). Left out (both crash): the original's Init without "Border" (the port's fix, tested alone), the operations
  before Init, an order button with a commanded index but no soldier, closing with a null member. The original's
  SetScale writes m_scaleW from a stack word (not compared); a follow order's data.w and PickTarget's time (the
  original's __fastcall PickTarget: EDX) are stack words, not compared.
- **Results:** 9396798 checks, 0 failures; all 390 measured lines of the range (the helpers used here and the
  functions above) run.
- **Port bugs found and fixed (on main; port mistakes):** PressOrderButton asked for the commanded soldier for every
  button (the original in the follow and wait cases only) and his order count once (the original again for the last
  order); ShowHint set the titles in array order (the original sets the options' before the camera's); the wait order's
  direction without orders was normalised as x² + y² + z² (the original's inline copy: (y² + z²) + x²).
- **Original's weaknesses (the port's fixes, tested alone):** Init updated "Border" without checking it.
- **Not tested here:** the 74b / 74c functions (patched: their parts).
- **Mutations:** 747 mutants, 83 not building, 653 of 664 killed, 11 equivalent
  (mutate_interface_command.py).
- **Harness lessons (my mistakes):** the start pace drawn from the random generator on each side (the sides
  differed); [[noreturn]] stubs and GCC's interprocedural analysis broke the patched functions (an endless loop, a
  dropped store of the wait order's direction: three false port differences); the first mutation run built without
  those flags (mutrun.py takes its own: the baseline failed and every mutant read "killed"; rerun).

## Human part 74b (the command map's orders, markers, lines and brackets, `interface_command.cpp` lines 253-374 and 941-1338, 2026-11-20)

- **The build:** `interface/build_commandb.bat` (`commandb_diff.cpp`, made from part 74a's harness: the same panel,
  world, fakes and set-up, run for real). The 74c functions these call (PickTarget, ShowRollMenu) are patched on both
  sides to the same loggers, with the same build flags as 74a (MUT_DEFS for the mutation runner).
- **Fakes added:** the humans' order requests (AllocHumanOrder from a pool of new orders, AddOrder, InsertOrder,
  RemoveOrder, SetOrder, HaveOrdersChanged; the change's InitPlain and destructor), the orders' UpdatePath and their
  paths (1 to 5 points), the info selector's GetSelectedIndex, the actors' vtables (GetBoxCorners 0xDC, GetSeatCount,
  GetSeatOccupant; the port's slots from its member pointers), the humans' vehicles and seats, the frames' and the game
  scene's render stamps, the game camera, the game scene's TransformPoints (now and then moved off the screen), the
  graph's SetTexture, SetWorldMatrix and DrawPrimitives (the screen lines' vertices a log line each, the order lines'
  by a hash), the cursor's SetFromWorld, the roll menu's Render, the frames' children (a pool frame by the parent and
  the name). The member lines snapped by a hash of their vertices; the members' first path vertices start not zero.
- **The tests:** 74a's scenarios with 15 more operations: RemoveSelectedOrder, RemoveOrderAt, AddOrder, AddActorOrder,
  ApplyFlags, AcquireOrderMarker, ReleaseOrderMarker, UpdateOrderMarker, AddPath, UpdateMember, RenderOverlay, Render,
  RenderMarkers, DrawBrackets (against each of the original's three copies 0x004bb260, 0x004bc300, 0x004bd3d0) and
  DrawBox; the input's state also sets the target and the members' route counts; now and then the camera faces along z
  with the humans in its plane (the facing test's tie). Left out (the original writes outside the panel): AddPath for a
  member outside the six buffers or with an empty path, UpdateMember for a seventh member (the port's fixes, tested
  alone). Stack words not compared: the w of AddActorOrder's and ApplyFlags' zero vectors (3 components in the
  original) and of a follow order's data, the screen lines' specular.
- **Results:** 9,750,370 checks, 0 failures; all 343 measured lines of the range run (XYZ, used only by 74c's input,
  left for 74c).
- **Port bugs found and fixed (on main; port mistakes):** AddOrder asked for the selected index for every order (the
  original skips it for a follow order and asks again for the insertion's index); AllocHumanOrder and InitPlain got
  the pace, stance and mode (the original passes the mode before the stance: ORed, the same flags, now in its order);
  the brackets asked the graph for the screen's size again for their growth (the original uses the box's);
  PrepareMarkerPart fetched the material once (the original goes through the mesh, level and face group for each of its
  five uses).
- **Original's weaknesses (the port's fixes, tested alone):** AddPath wrote past the six members' buffers and past a
  buffer's points, and an empty path left the segment count at -1 (the next path written before the buffer);
  UpdateMember wrote a seventh member's lines past the panel (the port keeps its markers up to date, a guard after the
  panel checked).
- **Not tested here:** the 74c functions (patched: part 74c).
- **Mutations:** 660 mutants, 74 not building, 571 of 586 killed, 15 equivalent
  (mutate_interface_commandb.py).
- **Harness lessons (my mistakes):** the frames had no children (FindChildFrame null: the first run crashed); the
  original's AddPath for member -1 wrote into the pristine copy before its panel (a later round's start differed: skip
  guards); Log's 600-byte buffer cut the brackets' vertex line after seven vertices (the bottom corners were not
  compared: 18 survivors); Init got 4 state icons where the markers read up to the 20th (null icons: 30 survivors).

## Human part 74c (the command map's mouse and input, `interface_command.cpp` lines 250-252 and 1339-1888, 2026-11-20)

- **The build:** `interface/build_commandc.bat` (`commandc_diff.cpp`, made from part 74b's harness: the same panel,
  world, fakes and set-up; parts 74a and 74b run for real). Nothing of the file is patched any more (the build flags
  kept).
- **Fakes added:** the collision world's RayTest (the original's thunk 0x00718830, __thiscall) and CollisionRayTest
  (0x00718770): the world's hit (one of four volumes of the world's: flags, type, actor, a frame whose data is an order;
  or nothing), point and normal (flat, steep, upside down, random); the game scene's UnmapScreenPoint; the cursor's
  GetScreenPos, SetMode (its mode written in place and kept), ClampMotion and clip rectangle (snapped by its frame); the
  info selector's SelectOrder; the team's SetCommanded; the roll menu's HandleInput, IsCursorOver, Select, MoveTo,
  Prepare, Open (its open flag written from the world's answer) and SetTarget, its choice, target and actions written in
  place; the controls' ClearKey; the graph's GetAsciiKey and KeyToChar; the profile's GetDetailSettings (the inverted
  mouse); the camera's SetTacticalMotion; the visuals' UpdateWBoundProc. The humans' faded flag and position, a static
  weapon's state, an animation actor's no-info flag, the orders' soldiers; more kinds of actors (cars, static weapons,
  submarines, animation actors). The panel snapped with the ground under the cursor, the dragged marker and its grip.
- **The tests:** 74b's scenarios with 11 more operations: IsOnMap (at and beside its edges), PickTarget, OnLeftPress,
  OnLeftDrag, OnLeftRelease, OnRightPress, OnRightRelease, ShowRollMenu, HandleInput, UpdateCursorMode and MoveCamera
  (their held and pressed arrays a few of the map's keys and buttons from a seed, hashed after the call), new rays now
  and then; the cursor now and then on the map, over nothing and exactly on the edges. Left out (both crash): a click
  or key with a commanded index but no soldier (74a's order buttons, OnRightPress's position), a marker dragged without
  a commanded soldier. Stack words not compared: the w of OnRightPress's and OnLeftDrag's orders' vectors.
- **Results:** 9,389,165 checks, 0 failures; all 377 measured lines of the range run (with parts 74a and 74b every line
  of the file).
- **Port bugs found and fixed (on main; port mistakes):** OnLeftDrag asked for the selected index before the order
  count and once for the order after the dragged one (the original asks for the count first and for the index again for
  its SetOrder); OnRightPress normalised the move order's direction as x² + y² + z² (the original's inline copy: (z² +
  y²) + x²).
- **Not tested here:** the dedicated server's HandleInput (no keyboard: not compiled here).
- **Mutations:** 651 mutants, 92 not building, 550 of 559 killed, 9 equivalent
  (mutate_interface_commandc.py).
- **Harness lessons (my mistakes):** the cursor's clip pointer carried over between the sides and rounds (the cursor
  block is not restored); the roll menu's actions written where its entries keep their texts (the vehicle pick-up never
  ran; I first swapped a struct that was right, the signals panel's, and put it back); OnLeftDrag's direction changed to
  the game's sum order from a misread of the asm (the test showed the original sums x first there; put back).

## Human part 75a (the game interface's life cycle, `interface.cpp` lines 124-152 and 185-431, 2026-11-21)

- **The build:** `interface/build_ui.bat` (`ui_diff.cpp`, including `interface.cpp`, with `ui_common.inc`: what the
  part 75 tests share). The port's C_Interface and the original's (0x6B900 bytes, built by its constructor 0x0049a420)
  are built once and restored before each scenario. Every method of the panels and the game's objects the file calls is
  faked on both sides by generated loggers (`ui_fakes.inc`: the scratch generator reads each method's declaration and
  address from the headers; the original's address redirected, the port's method defined); the engine's objects by
  generated slot loggers (`ui_vt.inc`: the scene, the driver, cameras, lights, sounds, sectors, models, the sound
  driver; each kind of frame its own vtable, I3D_frame's slots under the kit's). Answers drawn from the scenario's seed,
  the method's name and its count of calls on the side. The model and sound caches are real on both sides
  (`game/resource_cache.cpp` compiled in, -DINTERFACE_REAL_CACHES; the original's Init inlines the model cache's
  OpenInto): the original's built by their constructors 0x006d5030 / 0x006d5370, two search directories each, emptied
  before each side. The original's atexit (0x007e562e: its static constructors' destructor registration), sprintf
  (0x007e6334: the unstarted CRT's locks) and dbgPrintf redirected.
- **The world:** the interface's own scene (its primary sector and viewport inline fields), the model's frames found or
  missing by name, the model's Open in either search directory or none, the driver's states, the play mode and game
  role, the camera module (its methods through its vtable on the original's side), the squad commander; the panels'
  fields the file reads inline (frames, the member panels' humans, the radar, flags and counts: `PANEL_FIELDS`, both
  sides' offsets); the information screen's hints.
- **The tests:** scenarios of Init (mostly first), then up to 14 of Init, Done, Reset, InitPanels, DonePanels,
  InitDownPanel, ClearPicks, LoadStateIcons, LoadActionIcons, ClearMoveControls, ClearAllControls,
  LookAtOnCommandMap, the fields as other parts leave them, the world changing. Left out (both crash): the icons and
  the lower panel looked for without a scene. The port's fix, tested alone: Init where no directory has the model (the
  original keeps the model it made, leaked; the port releases it); the destructor.
- **Results:** 4,863,241 checks, 0 failures; all 208 measured lines of the range run (206 measured on 2026-11-21's
  rerun: all run).
- **Port bugs found:** none.
- **Original's weaknesses (the port's fixes, tested alone):** Init without the model kept (and leaked) it.
- **Not tested here:** the file's helpers the later parts use (PlaceExplosiveTimer, IsSeatVehicle, Distance).
- **Mutations:** 287 mutants, 38 not building, 244 of 249 killed, 5 equivalent
  (mutate_interface_ui.py).
- **Harness lessons (my mistakes):** slot fakes with the wrong number of arguments (the model's Open and EnumFrames:
  declarations over two lines the generator skipped; the kit's one-argument catch-all left in the frames' slots) moved
  the stack and showed as a clobbered register later; the debugging prints went to stderr, which the relaunched copy
  loses; the inline fields of the panels (frames, the radar) found one crash at a time before I listed them all.

## Human part 75b (the game interface's squad and small setters, `interface.cpp` lines 119-122, 436-612, 1302-1324 and 1446-1523, 2026-11-21)

- **The build:** part 75a's (`interface/build_ui.bat`, `ui_diff.cpp`'s "Squad" case).
- **The world added:** the team's member count (up to six; more: the port's fix, left out), the squad commander's
  groups (its CollectGroup virtual on both sides: each side's vtable filling its vector, the original's an MSVC one),
  the active index (inline in the port: the team's field on both sides), the clock (the port's timeGetTime renamed to
  the harness's, the original's import slot 0x0080f3f8 patched) and the last member click (each side's static); the
  original's inventory Refresh (0x004a26d0, no address in the header) faked like the port's. The panels' inline fields
  snapped after each operation (the stance panel's timer, the map's frame, ...).
- **The tests:** scenarios of Init then up to 14 of AddSquadMember, RemoveSquadMember, RemoveSquadMembers,
  RemoveFromMap, ShowEnemy, GetShownSquadMember, ShowSquadMember, HighlightMembers, SwitchToMember, OnMemberClicked,
  OnMemberClicked2, FilterInput, IsCapturingInput, AddMessage, SetTimer, ShowRoundResult, ShowCountdown, ShowMessage,
  ShowTimer, ShowSoundDirection, ShowScores, ShowRespawnScreen, IsRespawnScreenShown, FlashDamage, SetObjectiveIcon,
  ShowPhotoFlash, RefreshInventory, Done, the clock moving (exactly 300 ms now and then), the world changing.
- **Results:** 7,403,318 checks, 0 failures; all 158 measured lines of the ranges run.
- **Port bugs found and fixed (on main; port mistakes):** HighlightMembers read the member count once, before the
  commanded member and the group (the original asks for those first and for the count again at each member);
  ShowSquadMember asked for the selected member once (the original again for the panel).
- **Original's weaknesses (the port's fixes, tested alone):** HighlightMembers went through every member, past the six
  panels, with a squad of more.
- **Mutations:** 197 mutants, 21 not building, 176 of 176 killed, 0 survived
  (mutate_interface_uib.py).
- **Harness lessons (my mistakes):** the original's inventory Refresh ran for real (its header declaration has no
  address, so the generator left it: a crash); my edit scripts again met CRLF files and heredoc backslashes.

## Human part 75c (the game interface's drawing, `interface.cpp` lines 614-788, 2026-11-21)

- **The build:** part 75a's (`interface/build_ui.bat`, `ui_diff.cpp`'s "Draw" case).
- **The world added:** the camera module's scripted view and the console's NO_GI (answers the world sets: a hook for
  bools in `ui_common.inc`), the humans as actors (GetBoxCorners 0xDC through each side's vtable; the boxes' sizes),
  the game scene's TransformPoints (the points moved off the screen and to its edges), the graph's SetTexture and
  DrawPrimitives (every vertex logged but the unset specular); the panels' fields (their frames shown or hidden, the
  menus open, the counts) drawn again between operations.
- **The tests:** scenarios of Init then up to 14 of RenderOverlay, Render, DrawPickOutline, the fields (the panel's
  position, the members, the picked and usable actors, the forced screens), the panels' fields drawn again, the world
  changing, Done. Left out (both crash): Render on the network screen before Init (the null scene drawn).
- **Results:** 10,038,264 checks (all part 75 cases), 0 failures; all 118 measured lines of the range run.
- **Port bugs found:** none.
- **Mutations:** 210 mutants, 25 not building, 169 of 185 killed, 16 not observable
  (mutate_interface_uic.py).
- **Harness lessons (my mistakes):** the panels' values drawn again on the original's side carried into the port's
  start (restored per side now); the fakes' block first went outside the harness's namespace.

## Human part 75d (the game interface's updates, `interface.cpp` lines 154-174 and 793-1068, 2026-11-21)

- **The build:** part 75a's (`interface/build_ui.bat`, `ui_diff.cpp`'s "Update" case).
- **The world added (`ui_common.inc`):** the soldiers' fields on both sides (type, frame, vehicle, seat, the
  inventory's changed flag, the body frame; a door's flag), three vehicles (each side its blocks; GetSeatFlags and
  IsSeatFirstPersonOnly through each side's vtable; a car's physics at each side's offset: speed, fuel, tank), two
  weapons (the item, its definition with GetBehaviour through its vtable and its secondary component's kind and aim
  mode), the rays (C_CollisionWorld::RayTest and the camera module's GetCameraDir hand-written: their out parameters;
  the hits' volumes, flags and points, now and then at exactly 2, 0.65 and 0.2 from a body at the origin), the game's
  paused flag and first-person view, the menus open, the soldiers' camera dummies (the frames' children).
- **The tests:** scenarios of Init then up to 14 of Update, UpdateCrosshair, UpdatePick, UpdateCompassShown, the
  fields, the panels' and the soldiers' worlds drawn again, the clock, Done. The port's fixes, tested alone: the
  crosshair, the pick and the speedometer without an active soldier (the original went on with the null soldier).
- **Results:** 12,829,750 checks (all part 75 cases), 0 failures; all 179 measured lines of the ranges run.
- **Port bugs found:** none.
- **Original's weaknesses (the port's fixes, tested alone):** UpdateCrosshair, UpdatePick and Update's speedometer
  read the active soldier unchecked.
- **Mutations:** 258 mutants, 50 not building, 208 of 208 killed, 0 survived
  (mutate_interface_uid.py).
- **Harness lessons (my mistakes):** the generated fakes of methods with out parameters (RayTest, GetCameraDir) left
  them unwritten (hand-written now); the hashed answers made the menus open seven times in eight, so the speedometer
  was almost never reached until the world set them; a car's physics named by offset differed between the sides.

## Human part 75e (the game interface's input, `interface.cpp` lines 1073-1300, 2026-11-21)

- **The build:** part 75a's (`interface/build_ui.bat`, `ui_diff.cpp`'s "Input" case, three times the rounds).
- **The world added:** the held and pressed arrays from a seed (the interface's keys: the menu, pause, the messages,
  the chat, the map, the information, the soldier switch, the lower panel, the weapons, the inventory, the command map,
  the roll menu, the clicks, the signals, the soldiers' selection; the soldier's controls), the keys held down (Alt,
  Shift: the graph's IsKeyDown), the members' health (IsDead inline) and IsLocalPlayer, the signals' IsOpen call by
  call, the roll menu's Open and the map's Show hand-written (their flags set), the hovered id, a calm profile of the
  panels half the time (the menus closed, the map and the weapon selector hidden), the lower panel always in the model,
  the fields operation setting the member panels six times in ten (HandleInput's lower half: about 400 calls of a
  full run before, 650 now).
- **The tests:** scenarios of Init then up to 14 of HandleInput, ReturnControlOnInput, CloseAll, the fields, the
  panels' and the soldiers' worlds drawn again, the clock, Done; the arrays hashed after each call. Left out (both
  crash): the input on an interface the fields operation made active without Init (no scene, no lower panel).
- **Results:** 17,196,700 checks (all part 75 cases), 0 failures; all 156 measured lines of the range run.
- **Port bugs found:** none.
- **Mutations:** 289 mutants, 37 not building, 247 of 252 killed, 5 equivalent
  (mutate_interface_uie.py; rerun after the generator fix below: 244 at first, 247 again with the member panels
  shown more often).
- **Harness lessons (my mistakes):** the hashed answers never gave a member's hovered id (100..105), so the click
  handling was never observable; the generated Open / Show fakes left the menus' flags as they were; the rare key
  combinations needed more rounds and a calm profile before the mutations saw them. The world, the panels' and the
  soldiers' operations reseeded the shared scenario generator without keeping it, so the scenarios after one fell
  into a short cycle once a seed recurred (found in part 77; kept and restored now, all part 75 cases rerun: the
  same results but this case's, whose signals path the old cycle had reached by luck).

## Human part 75f (the game interface's panels, screens and saves, `interface.cpp` lines 1326-1445 and 1524-1696, 2026-11-21)

- **The build:** part 75a's (`interface/build_ui.bat`, `ui_diff.cpp`'s "Panels" case), with the chunks real on both
  sides (`common/chunk.cpp`; the original's own, its saves inlining them) over data files in memory (the port's
  rw_data functions defined by the harness, the original's six imports redirected to them).
- **The world added:** the game's tactical mode, the save stamp (the original's MD5 stamp left to its buffer: the same
  bytes as the port's, which writes what it last loaded), the files a load reads (inside a chunk of the game's: the
  interface's chunk, some short or empty, the hints, the stamp, some short, the timer, unknown chunks around one of
  the interface's), the signals' OnTargetDeleted hand-written (the original's takes no argument: the generated fake
  popped one).
- **The tests:** scenarios of Init then up to 14 of ShowInventory, CycleDownPanel, RefreshDownPanel, SetViewMode,
  OnActorEvent, SetScreen, OpenCommandMap, SaveGame (followed by the next module's chunk; the files compared byte by
  byte), LoadGame, the fields, the panels' and the soldiers' worlds drawn again, Done. Left out (both crash): the lower
  panel cycled, placed or loaded before Init; the scope's view without a scope. Not compared: an empty timer chunk (the
  original's local a stack word), a corrupt save's stray byte. The port's fix, tested alone: a deleted human's radar
  blip.
- **Results:** 17,508,200 checks (all part 75 cases), 0 failures; all 199 measured lines of the ranges run; with parts
  75a-e every line of `interface.cpp` (1,019 of 1,019). Rerun after the generator fix (part 75e's lessons):
  17,594,367 checks, 0 failures; all 198 measured lines of the ranges run, and still every line of the file; the
  six mutation files' results the same.
- **Port bugs found and fixed (on main; port mistakes):** SetScreen(2) switched the objectives off once (the original
  twice, as the port's own comment said); LoadGame zeroed the interface chunk's value for each chunk (the original once,
  before its loop: a later chunk too short keeps the value read before).
- **Original's weaknesses (the port's fixes, tested alone):** OnActorEvent left a deleted human in the radar's blips
  (read after it was freed).
- **Mutations:** 249 mutants, 13 not building, 233 of 236 killed, 3 equivalent
  (mutate_interface_uif.py).
- **Harness lessons (my mistakes):** LoadGame read nothing until its file was wrapped in a parent chunk (its loop runs
  inside one); the MD5 stamp fake first copied the port's stamp over the original's loaded one; a stray byte made both
  sides read stack words; my edit scripts met heredoc backslashes and non-idempotent half-applied steps.

## Human part 76 (the first-person view, `first_person_view.cpp`, 2026-11-21)

- **The build:** `interface/build_fpv.bat`, `fpv_diff.cpp` (the view compiled into it; the interface kit's frames, the
  generated engine slot loggers of part 75, `fpv_fakes.inc` for the game's methods), the chunks real on both sides over
  the animation table in memory; suite `human.first_person_view`.
- **The world:** the game (its scene's first-person root and primary sector, the interface, the camera mode, the
  field of view), the camera module by hand (the fields of view kept, so the easing compares), soldiers (vehicle, seat,
  health, velocity, hands weapon, uniform, belt grenade, stance, view mode, put away) answering by the soldier asked,
  items and definitions (first-person models, magazines, behaviours, sounds, the uniforms' hands names; the original's
  inline GetItemDef table filled), the children and finds of the frames (the flash, the warhead, the dummies, the
  visual "a"), meshes with sleeve textures, animation sets with end times, the caches' answers, RandFloat, the table
  file (items, 13 events, variants, ratios; empty events and variants, missing ratios, the wrong root).
- **The tests:** "View": 6,000 scenarios from start fields an earlier life could leave, Init now and then, up to 16
  operations (all public methods and the private ones the original has on their own) with the world changing between
  them; "Targeted": 3,000 of the states that random scenarios seldom combine; "Destructors"; "Fixes" alone. Left out
  (both crash, or the original reads or writes outside its table): a variant without a file chunk or with an empty one
  (the reader a level too deep: the original then writes names far outside its table), PlayAnim of an item type or
  event outside the table, a Panzerfaust or belt grenade without a first-person model or the hands, ApplySleeves or the
  updates without a soldier or a model, a flash time without a flash, an idle event or a draw's sound the original
  takes for granted. Not compared: the first unused vector's w (a stack word in the original).
- **Results:** 1,544,028 checks (quick 2,500 rounds in the mutation runs), 0 failures; every line of
  `first_person_view.cpp` (607 of 607); a full run about 9 s.
- **Port bugs found and fixed (on main; port mistakes):** the fields of view eased at 0.00314159 a ms (0x3B4DE322) where
  the original's constant is 0x3B4DE32F; LoadAnimTable below the items stopped once the size left was not positive (the
  original goes on while it is not 0, HasMore) and kept an id per level (the original one, a failed Descend leaving the
  last); Refresh stopped the animations before asking whether the hands item is put away; Refresh and RefreshWeapon
  asked the Panzerfaust's rounds before looking for its warhead; Init left the third unused vector's w 0 (the
  original's 1, the rotation's).
- **Original's weaknesses (the port's fixes, tested alone):** LoadAnimTable writes wherever the chunk ids point; the
  warhead frame taken for granted; a uniform without a first-person model name; the idle event without a hands weapon;
  PlayAnim outside the table; the draw's sound of a type without a definition. Not this file's: the shared name reader
  (0x00415d00) does not end the names it reads (the game's FpvAnims.sav ends all 546; the port's ReadChunkString does).
- **Mutations:** 1,247 mutants, 64 not building, 1,141 of 1,183 killed, 42 equivalent or out of reach
  (mutate_first_person_view.py).
- **Harness lessons (my mistakes):** `#define private public` hid that C_chunk::Tell is private (the MSVC check found
  it; HasMore is the original's test anyway); a mutation run started before the last source edit (restarted);
  table files the original misreads into writes outside its table looked like port crashes until simulated
  (fpvsim.py); start fields the game cannot reach (a model with type -1, a flash time without a flash) crashed both
  sides; the body named by offset (g++ lays C_Human out otherwise); heredoc backslashes once more.

## Human part 77a (the squad, the co-op team and the enemy records, `team.cpp` lines 38-906 and 1120-1134, 2026-11-21)

- **The build:** `team/build_team.bat`, `team_diff.cpp` (team.cpp compiled into it; the interface kit's frames,
  `team_fakes.inc` for the game's methods), the chunks real on both sides over data files in memory; suite `human.team`.
- **The world:** soldiers (type, model and its place, health, vehicle and seat, role, fade, net and sight ids, the
  enemies each sees, the controller and its type and combat order, environment flags: the original's and the port's
  offsets written per side), vehicles (seat flags, the car's +0x5E0), the actors module's actors (some null), the
  mission's objectives (the original inlines AreObjectivesDecided), the game's role, the difficulty, the co-op flag, the
  models' collision volumes (EnumFrames), the answers of the faked methods (unconscious and spectating now and then).
- **The tests:** 6,000 scenarios: both sides' C_Team and C_CoopTeam built (the original's by its constructors), up to
  six members added, up to 20 operations (every method, the records' Add / Remove / tactical view, saves followed by the
  next module's chunk and compared byte by byte, loads from files the world builds: members' ids, names, unknown chunks,
  the wrong chunk, empty files, the next module's chunk after; where the reader is left logged), crafted sequences
  (an enemy seen twice / lost / forgotten, the selected member apart from the active one, the commanded member's death,
  network games), the world changing between them, both destroyed; "Fixes" alone. Left out: removing the active member
  (the port's fix), what reads an index left past the members by a removal (both read past), a volume of an empty
  list, switching without an active member.
- **Results:** 807,876 checks, 0 failures; every line of the part's ranges (487 of 487); a full run about 2 s.
- **Port bugs found and fixed (on main; port mistakes):** OnMemberDied printed "%s is dead !!!" with Print; the
  original uses PrintColored in red (255, 0, 0) (both empty in the release build).
- **Original's weaknesses (the port's fixes, tested alone):** RemoveMember of the active member showed the member moved
  into its slot and read past the members when it was the last one. Not fixed by the port: C_TeamBase::LoadGame reads
  the name into a buffer it does not end (the game's saves always end it).
- **Mutations:** 716 mutants, 48 not building, 624 of 668 killed, 44 equivalent or out of reach (mutate_team.py;
  regenerated in part 77b: see there).
- **Harness lessons (my mistakes):** reseeding the shared scenario generator inside an operation (a world change, a
  load's file) made the scenarios repeat a 65-round cycle; the generator is now kept around those reseeds here and in
  part 76's fpv_diff.cpp (still 0 failures and every line; a targeted case added for the belt grenade's throw ending
  with the weapon put away, which the cycle had happened to reach). The interface harnesses of parts 64-75 reseed the
  same way (a separate task). Also: _exit skipped gcov's counts; a skip of every index past the members hid the
  selected member's removal; the generated IsUnconscious was mostly true, so the records were never lost.

## Human part 77b (the formations and the team manager, `team.cpp` lines 907-1123, 2026-11-21)

- **The build:** part 77a's (`team/team_diff.cpp`, the "Manager" case): both sides' C_TeamManager (the original's by its
  constructor; its C_Module base registering itself in the original's module table), its player and co-op teams the
  team operations' teams; the formation shapes faked by hand (constructed, destroyed, SetLeader keeping the leader at
  +0x74 that GetLeader reads inline, named by their order while they live: the allocator reuses their addresses).
- **The tests:** 6,000 scenarios of the team's operations and, half of them, the manager's and its formations': Tick,
  StartMission / StopMission / RestartMission (server and client), GetFormation (found or made), joining and leaving
  (script-driven soldiers more often), RemoveFromFormation, DestroyFormation (also of a pointer that is none), the
  formations' Tick, saves (followed by the next module's chunk, compared byte by byte) and loads (with or without the
  shape's chunk, unknown chunks, empty files), the manager's saves and loads (the team's chunk, formations by an
  existing soldier's id); the snapshot: the formations' members, name, id, shape, leader and scripted flag, the
  manager's flags. "Fixes": the manager's load of an empty formation chunk or of an id no actor has. Left out: a
  formation for no leader and no human into a formation (both crash); files whose reads run past their chunks.
- **Results:** 1,628,733 checks (both cases), 0 failures; every line of `team.cpp` but one (611 of 612): SaveGame's
  `continue` for a null formation (nothing puts one in the list).
- **Port bugs found and fixed (on main; port mistakes):** the four load loops (C_TeamBase, C_Team, C_Formation,
  C_TeamManager) stopped once the size left was not positive; the original's go on while it is not 0 (HasMore: a
  formation's chunk without its shape chunk leaves its loop a level too deep, and the original then reads on).
- **Original's weaknesses (the port's fixes, tested alone):** the manager's load read the leader's id also from an
  empty formation chunk and made a formation for whatever it found (a crash for none).
- **Mutations:** 150 mutants, 5 not building, 136 of 145 killed, 9 equivalent or out of reach (mutate_team_b.py).
  Part 77a's file regenerated on the changed source: 714 mutants, 48 not building, 622 of 666 killed, 44 survived.
- **Harness lessons (my mistakes):** the original reaches the squad commander at its global as well as through
  GetSquadCommander; shapes named by address got the allocator's reused addresses mixed up; files short of ids made
  both sides read past the file (the original's id then a stack word) or loop for ever: left out; a change to the
  source after part 77a's mutation file was made broke five of its entries: regenerated and rerun.

## Human part 78a (the campaign files, `campaign.cpp`: the nodes' formats, LoadFiles, Clear, 2026-11-21)

- **The build:** `campaign/build_campaign.bat`, `campaign_diff.cpp` (campaign.cpp compiled into it), the chunks real on
  both sides over data files in memory; suite `human.campaign` (needs the game data: HD2_UNPACKED, default
  `H:\HD2_Unpacked`).
- **The world:** the shipped `GameData\Gamedata00.gdt` and `01` (both well formed: every item and texts chunk 16 bytes,
  every string ended), and files the scenario builds: the complete format (roots, operations, missions, objectives,
  videos, screens with items and diary texts, plays, unknown chunks, missing ones) and the definition format (another
  version: the parsers' fixed parts, strings of another id, roots too short for their header), files the loading stops
  at (no version chunk, no root), an empty file after a loaded one, a root without its version chunk.
- **The tests:** 4,000 scenarios: both sides' C_Campaign (the original's laid out as C_GameProfile's constructor inlines
  it: its vtable, the multiplayer map's mission by its constructor, the single mission), LoadFiles, Clear, every
  campaign's Save and SaveProgress (followed by the next chunk, compared byte by byte), destroyed; the trees compared
  node by node after each operation. Every line of the part's ranges but one (375 of 376): SaveChildren's `continue` for
  a node of another type (the loaders make only the five child types).
- **Results:** 151,309 checks, 0 failures; a full run about 23 s.
- **Port bugs found and fixed (on main; port mistakes):** every chunk loop of campaign.cpp (the 15 loaders) reset its
  chunk id each time round and stopped once the size left was not positive; the original's keep one id (a header that
  cannot be read leaves the last one) and go on while the size left is not 0 (HasMore), as in team.cpp (part 77b);
  LoadFiles kept an id per file and needed the version chunk: the original keeps one for every file (an empty file after
  a loaded one is read as a root, the last id still 1) and reads a root that comes first as definitions. The port's note
  that only damaged files differ is gone.
- **Original's weaknesses (not fixed: damaged files only):** a short screen item keeps the previous item's bytes (a
  stack word for the first; the port zeroes them); the shared string reader does not end its buffer, so a string of no
  bytes shows the string read before; chunk sizes past their container or into a sibling read strings of no bytes or
  loop for ever.
- **Mutations:** 424 mutants, 64 not building, 291 of 360 killed, 69 equivalent or out of reach (mutate_campaign.py).
- **Harness lessons (my mistakes):** the shipped files checked for short chunks first (none) before damaged generated
  ones were blamed on the port; a damaged-size generator hung both sides (a header smaller than 6 bytes seeks back): taken
  out; my edit scripts' heredocs collapsed backslashes once more (the Edit tool for those lines).

## Human part 78b (the campaign's flow, queries, progress and missions, `campaign.cpp` lines 1-80, 109-113, 282-307, 321-413, 466-530, 564-575, 619-633, 645-676, 801-869 and 980-1439, 2026-11-21)

- **The build:** part 78a's (`campaign/campaign_diff.cpp`, the "Flow" case; suite `human.campaign`).
- **The world:** the files of 78a's scenarios loaded, then a progress file built from their trees (mostly as the game
  leaves it: the children before a cursor walked through, those after it not started; also random cursors, a few out
  of range, chunks missing or of other ids, an objective too many, the older one-campaign layout, an unknown chunk);
  the play mode's single mission and the session type; a multiplayer map on each side (the port's C_MpMap, the
  original's 0x48-byte block: names, ints, objectives, items, presets).
- **The tests:** 2,000 flow scenarios (and 78a's file ones, now 2,000: 193,775 checks, about 28 s): the flow
  (AdvanceToMission, NextStep, PreviousScreen, GetCurrentMission), every query with arguments in and out of range,
  SetCurrentIndex (negative too), SetupSingleMission, SetupMultiplayerMission, SaveGame (compared byte by byte, then
  the next chunk), LoadGame (where it leaves the reader), the containers' Next, Prev, GetCurrent, Reset, GetCursorState,
  GetChild, the per-type queries (the original's copies per type), the missions' DeactivateObjectives, CreateObjective,
  Clear; the trees compared after each (now with the multiplayer mission's map name, presets, ints, items and its
  objectives' fields). Every line of the part's ranges but two (443 of 445): GetMpObjective (not called in this file);
  78a's ranges still 375 of 376.
- **Port bug found and fixed (on main):** NextStep, PreviousScreen, AdvanceToMission and GetCurrentMission compared the
  current campaign signed; the original's compare is unsigned, so a negative one (SetCurrentIndex, a progress file)
  returns at once where the port read before the vector.
- **Original's weaknesses (not fixed):** Prev on a container at cursor 0 without children (where Next leaves an empty
  one) reads child -1, and one with a cursor past its end (a damaged progress file) reads past the children: both
  crash or read heap bytes; NextStep in single-mission play past the mission's end, and PreviousScreen stepping back
  across from its first screen, loop for ever (the set-up's end node and the play keep the game from both); the
  multiplayer mission's CreateObjective leaves the objective's team, texts and type unset (the port zeroes them);
  SetupMultiplayerMission's items get a stack word as their fourth; GetObjectiveStatus with campaign 0 reads the word
  before the vector (the port's earlier fix returns -1; not generated).
- **Mutations:** 674 mutants, 111 not building, 491 of 563 killed, 72 equivalent or out of reach
  (mutate_campaign_b.py).
- **Harness lessons (my mistakes):** the original's function statics register with its CRT's atexit, which crashed
  until redirected; the first Prev guard skipped Prev on any tree with a walked-through empty container, so stepping
  back into a first child went untested (now a dry run of the exact path); random progress cursors made such trees
  common (now mostly the game's); the multiplayer mission's own fields were not compared at first; the first mutation
  run used mutants generated before the unsigned fix (nine did not apply).

## Human part 79 (the game's saves, loads and restarts, `game_save.cpp`, 2026-11-22)

- **The build:** `game_save/game_save_diff.cpp` (suite `human.game_save`): game_save.cpp compiled in, the original's
  five functions called in place; what they reach faked on both sides with the same loggers (the checkpoints, the
  interface, the first person view and the quick messages: resets, saves writing a marked chunk, loads reading one; the
  modules, the timed events, the mission and its objectives, the campaign, the profile, the network game's players, the
  actors module's actors and spawn points, the mission squad, the application, the speech sets' delays, the stains, the
  music, the texts and messages); the chunks real on both sides. The original's three calls into its copy protection
  (CALL [0x00a59000]) are patched out.
- **The world:** the game's fields (state, mode, clocks, switches, saves used, pending spawns, the mission's name), the
  static state record, the role, the difficulty, the mission and its objectives' statuses, the current campaign and
  mission number, the shared lives and the players, actors by id, the squad's soldiers deployed or not.
- **The tests:** 60,000 scenarios of up to 12 operations (3.5 million checks, about 12 s): CanQuickSave, RestartMission
  (new round or not), SaveGame (the file compared byte by byte, then the next chunk), LoadGame of generated files
  (chunks in any order, missing or doubled, versions and mission names that differ, long names, short and long state
  records, unknown ids, a child chunk running past its parent, another top chunk, an empty file), LoadGame of the
  side's own last save, LoadSavedGame, the chunk not open, the world, the role and the state changed between them; the
  game's fields, the static record, the players' lives and the speech delays compared after each. Every line of
  game_save.cpp runs (224 of 224).
- **Port bug found and fixed (on main):** LoadGame's loop ran while `RemainingSize() > 0`; the original's (as
  `HasMore()`) runs until the reader is exactly at the chunk's end, so after a child chunk running past its parent it
  reads on (the next chunks, then the last id's handler again at the file's end). The harness fails 758 times on the
  old loop.
- **Original's weaknesses (not fixed, not tested):** a short version, saves-used or signal pistol chunk leaves the
  original comparing or storing stack words (the version and the pistol uninitialized, the saves used the 0x29A
  header's first bytes; the port keeps 0 / the old count); a mission name with no terminator in the bytes read (or an
  empty name chunk) makes both compare uninitialized buffer bytes; a mission progress chunk without a mission calls
  through null in both; RestartMission frees only the pending spawns' records (the port's earlier fix frees their state
  and stats too: not observed here).
- **Mutations:** 430 mutants, 60 not building, 350 of 370 killed, 20 equivalent or out of reach (mutate_game_save.py).
- **Harness lessons (my mistakes):** the role change first drew from the shared generator, so the two sides ran with
  different roles; the players' lives were compared only up to the player count, so a loop running one slot past it
  survived (now every slot, one always spare).

## Human part 80 (the meleeTracker's animation mixer, `melee_tracker_anim_mixer.cpp`, 2026-11-22)

- **The build:** `melee_tracker/mtmix_diff.cpp` (suite `human.mtmix`, the interface kit): the mixer's source compiled
  in, the original's 17 functions called in place on a mixer of the same bytes (its layout is the original's). One fake
  model (the kit's frame: eight stages' animation, weight, time, flags and activity; FindChildFrame, SetCallback, Tick,
  SetAnimation .. AdvanceStages logged), a note frame "a" whose model's frame data names each side's tracker (the
  original reaches the mixer as the actor + 0xA0), fake animation sets, the animation cache's Get on both sides
  (0x0041fc10 redirected); the chunks real on both sides over files in memory. MixerBlendProgress is the original's on
  both sides (part 7 tested the port's).
- **The tests:** 60,000 scenarios of up to 14 operations (2.3 million checks, about 20 s): Init (now and then another
  model), Reset, UpdateStages (a time step or none, started or not, the gates), UpdateMove, PlayStages, SetAddMove,
  SetSlide (short, axis and unit offsets), the track getters and setters, TakeSoundEvents, GetBlendProgress, the note
  keys through NoteHook and OnNote, SaveGame (the file byte for byte, inside another chunk or not, the chunk closed),
  LoadGame of the side's own save or of generated files (sub-chunks in any order, missing, doubled, unknown, the stage
  chunks' six kinds, missing animation files, longer chunks, another top chunk, an empty file); the stages changed
  between them. The mixer's bytes, the model's stages, the sets' references and the note frame's callback compared
  after each. Every line runs (356 of 356).
- **Port bugs found and fixed (on main):**
  - ResetTracks cleared each track's events; the original clears only their count.
  - LoadGame's loop ran while bytes remained (and stopped on a failed header read); the original's runs until the
    reader is exactly at the chunk's end, a failed read keeping the last id (as part 79's).
  - LoadGame read only stage kind 2 as a time; the original reads kinds 2 to 5 so (3 to 5 are never written).
  - SetSlide summed the offset's square z, y, x; the original x, y, z (the step's velocity differed in its last bits).
  - LoadGame's comment called a missing animation file a crash in the original; it plays nothing, as the port.
- **Not compared:** the move's w (ResetTracks and UpdateMove store a stack word) and the vectors' w after a load (the
  original's stack copy); short chunks (the original keeps stack words); a fourth event in a track (the port's cap);
  times beyond 2^24 ms (the original's x87 code converts them exactly where the port rounds them to float: junk a save
  of the game's never holds).
- **Mutations:** 705 mutants, 49 not building, 621 of 656 killed, 35 equivalent or out of reach (mutate_mtmix.py).
- **Harness lessons (my mistakes):** the first load files filled every field with random words (float bits as times:
  billion-ms times that only differ by the conversion above); a Python edit script through a bash heredoc collapsed a
  backslash again.

## Human part 81a (the meleeTracker's movement without Update and Move, `melee_tracker_movement.cpp`, 2026-11-22)

- **The build:** `melee_tracker/mtmove_diff.cpp` (suite `human.mtmove`, the interface kit): the movement's source
  compiled in, part 80's mixer linked, the original's functions called in place on a movement of the same bytes.
  Each side has its own tracker block (the original's offsets and the port's), AI controller and target with
  vtables; fakes for the collision world (Add/Remove/MoveVolume, the sphere test), the sound module (Play logs the
  whole request, Stop), the game, the camera's SetViewDir, the scene's sectors, the graph's mouse, the player
  controller's MouseLook, the team, the animation cache's Get, _aligned_malloc/_aligned_free (the port's free
  through the test's too); the chunks real on both sides over files in memory; the original's fastcall helpers
  (AngleBetween2D, RotateAboutY/X, MeleeTrackerAnimClass) forwarded.
- **The tests:** 30,000 scenarios of up to 14 operations (1.24 million checks, about 37 s): Bind, Init, Reset,
  FreeVolumesAndSounds, SetOptimized(Controller), SetMode / SetSubModeA / B, the sequence getters, the queue's push
  and pops, SetNetPosition and ClearNetBit (tiny, whole-number and big steps), SetWorldDir (near the frame's
  forward), SetFacing (the facing itself, one component changed), TurnToFacing and TurnDir (angles near 1e-4),
  UpdateAnimation, HandleAnimEvents (footsteps, the lie-down slide with short directions), ApplyNetInterp,
  CheckGround, Bite, UpdateSpheres, StepAnimation, BuildSequence (biased toward the sub-mode transitions),
  CheckAttackSequence, PlayAnimSound, BarkTurn (the original's takes the tracker), MeleeTrackerAnimClass /
  OneShot, GetLungeRange, SaveGame (the file byte for byte) and LoadGame (its own save, generated files: sub-chunks
  in any order, missing, doubled, unknown, another top chunk, an empty file); the controller's answers changed
  between them. The movement's bytes, the volumes, the tracker's position and direction, the model's stages, the
  sets, the cooldown and the frames compared after each. Every line in range runs but one (879 of 880: the
  negative-rand branch).
- **Port bugs found and fixed (on main):**
  - SetNetPosition: the frame distance and the interpolation distance were summed x, y, z (the original z, y, x),
    the direction normalised x, y, z; the steps were computed in double from a distance over a fixed 1e-5 (the
    original divides by the last net step's length, 1e-5 only when it did not move, each operation rounded to
    float, and __ftol keeps the low word beyond 2^31); the velocity was the direction times the distance (the
    original: times that divisor); the frame test was <= 2 (a NaN differs: the original's is !(> 2)); the frame's
    position took w = 1 (the original copies the matrix's row 3 whole).
  - Init's net position took w = 1 (the matrix's row 3 whole, as above).
  - SaveGame / LoadGame's sub-chunks 0x27 and 0x28 were sound handles; they are the net clock m_388 and its length
    m_38c.
  - LoadGame: a wrong top chunk did not ascend; its loop stopped on a failed header read (the original's runs until
    the reader is exactly at the chunk's end, keeping the last id); 0x24, 0x2C, 0x2F stored the word's low byte (the
    original: != 0); the end added the volumes back and showed the frame, returning the mixer's result (the
    original moves the volumes, leaves the frame, ignores that result and returns true).
  - HandleAnimEvents: the footstep's w was 1 (the matrix's row 3 w); the slide's x and z offsets were 0 (the
    original multiplies the direction by 0: -0 for a negative one).
  - PlayAnimSound's class 2 sound went into the second handle; the original keeps it in the third.
  - TurnToFacing / TurnDir tested the angle against the float 1e-4 (the original's double).
  - Bite: a NaN distance or exactly 2.1 differed (the original's !(<= 2.1)), and the air counter was compared as a
    float (the original tests its bits: -0 counts as airborne).
  - StepAnimation turned the near attack into the far one only when the first id was 6 (the original also after
    the advance read the next one) and not for a NaN distance.
- **Not compared:** the original's stack words (the volumes' w, TurnDir's w, the mixer move's w, the net
  velocity's w, the loaded vectors' w); short sub-chunks; StepAnimation outside the animation table (both read past
  it); SetMode(5) when the queue never empties (both loop forever); a missing controller, anim2, body or ground frame
  (the original reads them unchecked).
- **The original's weaknesses (kept, the port shares them unless noted):** death (SetMode(5)) removes and frees its
  volume slots unchecked, a second death removing null (the port skips empty slots: a fix); its queue loop never
  ends when QueuePopBack's quirk leaves the read index out of 0..4; a sequence BuildSequence makes can lead
  StepAnimation past its table (a 0x17 read after a skipped one, an entry without an animation: anims[-1]); the
  controller, anim2, body and ground frames are read unchecked; negative time steps are read unsigned in places.
- **Mutations:** 1,747 mutants, 86 not building, 1,504 of 1,661 killed, 157 equivalent or out of reach
  (mutate_mtmove1.py).
- **Harness lessons (my mistakes):** the dummies' boxes were left empty (every sphere radius 0) and the frames'
  matrices had w 0 (the footstep's w unchecked) until the mutation run showed it; the volume frees were not logged;
  BuildSequence's sub-mode branches ran twice in 10,000 scenarios before the bias; a Python edit through a bash
  heredoc collapsed a backslash; run_suites.py's mtmove entry slipped into part 80's commit.

## Human part 81b (the meleeTracker's movement: Update, Move and their helpers, `melee_tracker_movement.cpp`, 2026-11-22)

- **The build:** part 81a's `melee_tracker/mtmove_diff.cpp` (suite `human.mtmove`), extended: the original's Update,
  Move, ContactResponse, AlignToSlope, SlopeSlide (its bundle of normal, flags and hit built by the test), IsGrounded,
  VolumeMaterial, SlideStep and GravityDistance called in place. New fakes on both sides: CollisionRayTest, the sphere
  test and the body sweep (MoveSpheresStepping: its whole request logged, then a scripted end, contacts and hit; now
  and then a climbed step or a changed move) answer from one scripted stream per scenario, taken in call order;
  Set/ClearIgnoredVolume(s), the debug trajectory line, the tracker's Hit (a tracker vtable), two sectors for the
  scene's GetSector; the materials' count and g_moveIterations set on both sides and compared.
- **The original's stack words:** Move reads stack it never wrote (the slope bundle's normal: the "moving" flag; the
  sweep spheres' w, which can reach the position's w). The test zeroes the stack before the original's Move (Update's
  call of it patched to a zeroing wrapper); the port takes them as 0 (as before: the slope-slide's moving path stays
  off). After a Move the tracker's position w and the w words copied from it (the net positions', the facing's, the
  move direction's, the mixer's add-move and slide) are not compared.
- **The tests:** 27,000 scenarios (lowered from 30,000 for the 40 s budget; about 38 s): 81a's operations plus Update,
  Move, ContactResponse, AlignToSlope, SlopeSlide, IsGrounded, VolumeMaterial, SlideStep and GravityDistance, with the
  mixer's move still, tiny, straight down or any; the fall speed, its phase, the landing fields, the timers biased to
  their tests' edges; the model's world position at the tracker's or a hair off, its forward unit, nearly vertical or
  short; contact normals at the floor tests' edges (0.707, 0.95, 0.55, 0.5). Every line of the range runs but one
  (Move's second `goto` to the end: the standing ray test only finds ground after zeroing the fall speed).
- **Port bugs found and fixed (on main):**
  - GravityDistance was a misreading of the decompile ("dimensional quirks"): the original sums the velocity times
    the time of each 85 ms step, the velocity gaining 9.81 / 3 at each; the port added the velocity itself at each step.
  - Move: the step climb's retry, when it hit nothing, wrote its result into the end position (the original: into
    the move, which ContactResponse then slides the body by); its tests read the sweep's move (the original: its own
    copy); the sweep's iterations were 4 near the target and 3 farther (the other way round); standing still across
    set the whole position from the tracker (the original only x and z, keeping the contact's height); the move's
    squared length summed x, y, z (the original z, x, y) and the frame's forward x, y, z (y, z, x); a NaN pitch
    snapped to -1 (the original +1) and a NaN |dir.y| left a sphere out; the ground flag was taken again after
    ContactResponse (the original keeps the one before); the move's w was 0 (the mixer's).
  - Update: the frame, put at the simulated position for the spheres, stayed there (the original puts it back where
    it was, with its sector, afterwards); the frame distance summed x, y, z (z, y, x); the damage step read the time
    signed (unsigned); the second volumes' w was the spheres' (the position's).
  - The counters at +0x240 and +0x24C were floats; the original's are integers (DEC, INC, CMP): GetLungeRange is 0
    after more than 5 airborne steps (the port: after 1), the sphere-sweep counter decremented as a float.
  - SlopeSlide: the "fully slid" test was ny < 0.55 (<=), the ease threshold the float 0.17 (the double), the normal's
    up part read alone (the original's (z + x) * 0 + y: an infinite x or z is no floor), and the dot's terms summed in
    the other order; NaN cases (the height difference, the fall speed, the normal, the ease direction, the blend) took
    the other branch.
  - AlignToSlope: the second normalisation and the no-ground one summed x, y, z (the original x, z, y); the direction's
    w was kept (the original's is the position's); a NaN step or tilt took the other branch.
  - ContactResponse: a NaN drop or move took the other branch.
  - IsGrounded read the normal's y alone (the original's (x + z) * 0 + y) and its count signed.
  - SafeLength compared the float (the original its bits as an int: a negative NaN is 0); NormalizeDir's NaN went to
    the axis snap (the original normalises it).
- **Not compared:** the stack words above; Move's slope-slide "moving" path (its normal is a stack word in the
  original's: the port keeps it off); the debug line's end w; SlopeSlide's end w on the paths where the original takes
  a stack word.
- **The original's weaknesses (kept):** Move's slope bundle normal and the sweep spheres' w are never written; IsGrounded
  reads every contact's volume unchecked (a null volume with a wall normal faults); Update moves the frame to the
  simulated position and back every frame the two differ.
- **Mutations:** 916 mutants, 41 not building, 782 of 875 killed, 93 equivalent or out of reach (mutate_mtmove2.py).
- **Harness lessons (my mistakes):** 81a's GetLungeRange test passed only because the counter's random values were
  float bits (1..6 as floats: huge integers), hiding the integer counter; random calls inside the operation (SlideStep's
  phase) made the sides differ; the sweep fake never climbed, so the step-climb retry did not run until it could lift
  the end; a heredoc patch with escaped quotes broke (written to a file instead).

## Human part 82a (the meleeTracker actor's sensing and per-frame code, `melee_tracker.cpp`, 2026-11-22)

- **The build:** `melee_tracker/mt_diff.cpp` (suite `human.mt`, the interface kit): the tracker's source compiled in,
  `melee_tracker_ai.cpp` linked (SetController's variants, not called here; its Disable through the linker's --wrap
  to the test's logger), the paths part 82b tests stubbed (`mt_stubs.inc`: a call ends the run). Each side has its
  tracker: the original's a block in its layout, the port's a C_MeleeTracker block (its C_Actor packs the vectors:
  every field elsewhere, set and compared by name through the offsetof table). Faked on both sides with the same
  loggers: the movement's members (part 81's), the controller (a block in the original's layout, which the port's
  class has: its virtuals by slot, each side's), the humans (blocks in each side's layout: type, frame, position,
  health, role, their locomotion's ScaleStanceVolumes, GetMoveSpeedClass), the sight module's targets (each side's
  vtable; the original's vector at 0x0089b298) and Remove(Observer|Target), the actors module's GetHuman, the sound
  module's RemoveListener, the controls' GetValues, the game's debug print, the head's joint override set (COM, shared),
  the frames (the kit's arena; the model's frame data naming the side's tracker); RandFloat, timeGetTime,
  QueryPerformanceCounter / MicrosecondsNow (the original's import slots) and rand alike; the sighting pool's
  _aligned_malloc the test's; the copy protection's check made to pass (HitDamage cuts a cracked copy's damage).
- **The tests:** 60,000 scenarios of up to 12 operations (1.5 million checks, about 22 s): SetOptimized, TestSight,
  ResetAI, OnSightEvent (spotted and lost, human and other kinds, in and out of range), Update, Tick (the fall-out
  height, the player-driven controller, the bite target's stance volumes, the look-around request by animation),
  TrackVelocity, HeadLook (the three turn-rate curves and the others, turning and starting, the return, the ~56 degree
  clamp; forwards unit, short or any), SetLookBehaviour, UpdateLookBehaviour (states 6 and 8, every repeat count),
  HitDamage (the seven bones), Hit (server and client, a kill, -1, a wound below an eighth), UnknownD0, OnSoundHeard
  (every sound chapter, refreshed and new sightings, the bullet-hit threshold), HearPlayers (the list empty, with spare
  room, and filled); whole-number positions now and then (distances exactly the sight or hearing range), equally near
  bones;
  the answers (the sequence's animation, the controller's target, the humans' pace) changed between them. The
  tracker's fields (the lists by their entries), the controller's weights and the frames' flags compared after each.
  Every line of those functions runs but HitDamage's skip of a missing bone (the port's fix: the harness always has
  them).
- **Port bugs found and fixed (on main):**
  - HeadLook: the facing and the head's direction were normalised by LS3D's Normalize (the original's inline sums y, z,
    x); the fixed turn rate was 0.0028f (the original's constant is one float above: 0x3B378035); the growing rate's
    constants were the floats 0.002 and 0.006 (the original's doubles: 0.002f and 0.0060000002849847078) and its
    logarithm rounded to float before the product (the x87's FYL2X is not rounded by the float precision); the clamp's
    limit was 0.9707963f (the original's 0.97079635f, 0x3F78861C); a NaN angle took the step and a NaN turn turned
    (the original takes the angle and counts it as no turn).
  - UpdateLookBehaviour: the facing normalised by LS3D's Normalize (y, z, x); the 1/11 step 0.0909091f (the
    original's 0.09090909f, one float below).
  - Tick: the controller's target asked once (the original asks twice: to test, then to use).
  - HitDamage: the bones' matrices brought up to date in the zones' order (the original's in the movement's); the
    squared distances summed x, y, z (the original's z, y, x, the last zone's x, y, z: equally near bones picked
    another zone).
  - OnSoundHeard: the distance was S_vector's Magnitude (the original sums y, z, x and takes the length as below).
  - SafeLength (OnSightEvent's, OnSoundHeard's) compared the float (the original its bits as an int: a negative NaN
    is 0) and took a NaN's length as 1 (the original's root).
  - HearPlayers: each player slot asked for once (the original asks three times: to test, for the role, to add).
- **Not compared:** the look point's and target's w (stack words where the original sets them); the pooled sightings'
  identity (each side's pool); SetController and the controllers' own code (part 37, and 82b); the mission life
  cycle, save and load (82b).
- **The original's weaknesses (kept, unless noted the port's fixes):** OnSightEvent reads a target past the sight list
  or in a free slot unchecked; Update, Tick, TestSight, ResetAI, Hit and UnknownD0 call the controller unchecked; the
  head look crashes without the head override set (a model without the bones); OnSoundHeard reads the listener frame's
  actor and its controller unchecked; HearPlayers reads the role of whatever actor a player slot holds; HitDamage reads
  every bone unchecked; the copy protection cuts a cracked copy's bite damage to a tenth (dropped).
- **Mutations:** 721 mutants, 58 not building, 625 of 663 killed, 38 equivalent or out of reach (mutate_mt1.py).
- **Harness lessons (my mistakes):** calling the port's virtuals on its block dispatched through the test's vtable
  (qualified calls now); the frames' Tick slot and the sighting pool's _aligned_malloc were missing (crashes); two
  heredoc patches collapsed `\n` (fixed with the editor); the decompile misread HeadLook's and HitDamage's sums and the
  turn's NaN sign: the asm settled them; the first mutation run showed the missing exact-distance, spare-capacity,
  look-around and tied-bone cases (added, the run repeated).

## Human part 82b (the meleeTracker actor's controller choice, save / load and mission life cycle, `melee_tracker.cpp`, 2026-11-22)

- **The build:** part 82a's `melee_tracker/mt_diff.cpp` (suite `human.mt`), with `mt_life.inc`: the life scenarios. The
  controllers are real on both sides (the port's from `melee_tracker_ai.cpp`, the original's its own code), their bytes
  compared after each operation in the original's layout (pointers by name). The chunks are real on both sides (each
  side's C_chunk) over files in memory (the dta functions faked alike). Faked with the same loggers: C_Actor's members
  (Activate, Deactivate, SetFrame, CopyFrom, SaveGame, LoadGame, the destructor's base); the movement's Bind,
  FreeVolumesAndSounds, Reset, SaveGame and LoadGame (the original's through its vtable); the model cache's OpenInto;
  the effects' FadeInModel; the driver's joint override set and item (Add, Remove, Release) and the model's
  SetJointOverrides; the sight module's AddObserver, AddTarget and SetTargetState; the sound module's AddListener; the
  actors module's actors, GetHuman (a slot answering none now and then) and GetActor (net ids 200 + i); the camera's
  SetViewDir; the game's checkpoints; the tracker's own ReloadModel slot. The shared animation table (0x008AC5C0) is
  set alike on both sides and compared after each operation.
- **The tests:** 25,000 life scenarios of up to 8 operations (1.27 million checks, about 20 s; the suite runs them
  after part 82a's 60,000): SetController (every mode, and an unknown one), SaveGame (the controller, the movement's and
  the actor's answers; the sightings with and without a source; the seen list), LoadGame of the side's own save (whole,
  cut at ten steps, at any byte, right after a leaf's header; the top chunk wrong, the state chunk wrong, the never-saved
  leaf 7 in place of 8, an empty file, the top header alone), WriteProperties, ReadProperties (whole and short), SetFrame
  (the actor's answer), CopyFrom, StartMission (server and client), StopMission, RestartMission, ReloadModel, SetupFrame
  (every bone there or one missing, a frame with and without a script, server, local and client, the players among the
  actors), and the destructor now and then as the last. The saved files compared whole (the sightings' source pointers
  as markers), the reader's position and chunk depth after each load, the depth after each save.
  Every line of those functions runs but LoadAnimTable (StartMission's when the table is empty: it inlines the original
  animation cache's internals), the constructor and the creator, and the port's own GetFullAI / GetScriptedAI.
- **Port bugs found and fixed (on main):**
  - LoadGame's opening: a wrong state chunk ascended twice (the original once: 0x385 stays open), and a missing state
    chunk header ascended (the original returns at once).
  - LoadGame's leaf loop: each chunk read into its own id, the loop ended by a failed header read and by the remaining
    size (the original: one id for every chunk, so a failed read keeps the last and its case runs again; the loops run
    until the reader is exactly at the chunk's end); the leaves read straight into the members (the original reads each
    into a local kept across the loop and copies it: a short leaf copies what the local last held).
  - LoadGame's sightings: allocated with time 0 (the original stamps timeGetTime, kept when the entry is short).
  - LoadGame's controller: a chunk other than 0 or a leaf other than 1 in it was left differently (the original ascends
    once, leaving what it entered open).
  - SetupFrame: each player slot asked for once (the original asks three times: to test, for the type and role, to add).
- **Not compared:** the old controller after SetController, StopMission and RestartMission free it; the tracker after
  its destructor (g++ drops the dead stores); the sightings ReleaseSightings returns to each side's pool; the start
  position's and the velocity's w (stack words in the original's).
- **The original's weaknesses (kept, unless noted the port's fixes):** SetController's unknown mode calls through a
  null controller (fixed), and LoadGame recreates the controller from the saved mode unchecked (fixed: the load fails);
  SaveGame saves the controller's mode unchecked (a crash without one); StopMission detaches the controller unchecked
  (fixed); LoadGame copies a stack word into the w of the start position, look point and look target (fixed: the w
  kept), reads an id leaf's stack word when the file ends in the sightings or the seen list, maps the never-saved leaf
  7 to the look return, and loops on a short file until the seeks past its end reach the chunk's end; SetupFrame skips
  the whole setup (no controller, no sight or sound registration) for a model missing one bone (kept), reads the role
  of whatever actor a player slot holds (fixed: humans only), leaves the look target's and the velocity's w unset
  (zeroed); the destructor frees only the three lists' arrays (the sightings and the controller go at StopMission).
- **Mutations:** 439 mutants, 40 not building, 361 of 399 killed, 38 equivalent or out of reach (mutate_mt2.py).
- **Harness lessons (my mistakes):** the saved files went through Log's 600-byte buffer, so only a save's first ~290
  bytes were compared (now pushed whole); a missing Ascend that Close made good was invisible until the depth was
  logged; the load's cuts fell at ten fixed steps only (now any byte and right after a leaf's header); m_538 was always
  0 and the sight id 0 rare; the wrong-chunk loads first failed on the opening's ascends, which the asm settled; two
  probes applied by hand to the source (restored, the diff checked); calling the port's virtuals unqualified,
  missing fakes (the frame's Tick, _aligned_malloc, the camera, ~C_Actor) and uninitialised original allocations
  crashed early runs.

## Script part 1 (the mission script compiler, `script_compiler.cpp` and the expression parser, 2026-11-22)

- **The build:** `script/sc_diff.cpp` (suite `script.compiler`, data needed): the port's `script_compiler.cpp`,
  `script_expression.cpp`, `script_tables*.cpp` and `script.cpp` (its building members) against the original's
  compiler (0x0042c850 on its global 0x008993f0, its constructor run first) compiling into a C_Script block of the
  original's (its constructor 0x0041dcb0). Faked alike: the dta functions (the unpacked data, or a copy in memory),
  GetGame's mission name (the original's C_str at +0x6A), the original's operator new / delete (frees held until its
  script is dumped: its GroupRange deletes a node the tree keeps; 16 guard bytes after each block, checked when freed).
  The compiled scripts are written out as text and compared: the result, the start point, the handlers, every command
  with its arguments, the numbers, strings, frames, whenevers, the signal / cutscene handler lists, the expression
  trees (walked together: a node the original freed is marked and not compared below), the compiler's last token,
  variables, labels and open blocks, and the files opened and closed in order. Not compared: the compiler's two line
  counters (+0x408, +0x40C), written and never read.
- **The tests:** every game script (5,347 under `SCRIPTS`; 5,304 compile, 43 fail in both) and 12 mutated copies of
  each (a token deleted, replaced from a vocabulary of tokens and whole statements, inserted, duplicated, swapped, the
  file cut; not on an include line); 30,000 random expressions from a grammar (every operator, mixed precedence,
  parentheses, NOT, a sign, the built-in functions with right and wrong arguments) in an If, an assignment and a
  Whenever; crafted files (a missing file, includes found in `Scripts\include` or missing, include forms both read
  alike, an include file with a NUL byte, start points, Else and handlers with and without blocks, a label defined
  twice, a control character, a script compiled over another); the string literals' printf formatting against the
  original's sprintf (every byte after a '%', 100,000 random strings of flags, widths, sizes and characters). About
  200,000 checks in 25 s. Every line of the compiler and the parser runs but the destructor (the global's), a null
  command in CheckProgram (never stored), a constant in the number table (the compiler adds none) and the client-only
  paths (AddCommand answers -1: the original crashes there). Cases where the original wrote past a block (21 of the
  mutated copies; guard bytes after each of its blocks) are reported and not compared; none is a shipped script.
- **Port bugs found and fixed (on main):**
  - String literals: "%B" kept the B (the original's printf drops a character its table classes as a conversion: the
    shipped `"%B_engvtabore.I3D"` in BURMA2's CUTHLIDKA2.scr loads `_engvtabore.I3D`); the literal formatting is now
    MSVC6's state machine (its lookup table 0x00817938): flags, widths and sizes dropped, a width's padding printed.
  - Whenever and SetWhenever names: kept as written (the original keeps and looks them up formatted, C_fstr(token)).
  - Numbers out of the int range: INT_MIN (the original's __ftol keeps the low word).
  - GoTo / Gosub to a defined label: the label list lacked the duplicate the original appends.
  - Includes: `#include ""` or a lone quote left the quotes in the source (the original's directive is any string
    token); the name was erased exactly (the original blanks the token's length plus one from the character after
    the word, wherever the name is: two spaces before it leave the name's end behind); "#include" elsewhere was kept
    (the original blanks every one, and the character after it); `#include"x"` included (the original's blanking eats
    the quote); a NUL byte in an include file was dropped with the rest of it (the original's script ends there).
- **The original's weaknesses (kept unless noted):** a client's compile crashes at the first '}' (AddCommand answers
  -1, the block's begin is read at -1: the port checks); a token of 1,024 characters or more overruns the compiler
  (the port truncates it); a script ending in "#include" hangs the preprocessor; only the first include of a file
  is honoured, and only when nothing but other directives precede it (the port honours each one at a line's start);
  GroupRange deletes the node its new group keeps (use after free: the port keeps it; no shipped script groups
  operators of mixed precedence without parentheses, so none reaches it); at a level with two operators more than
  operands it writes past its list (undefined; one more, "x == -1", is modelled: the last operand dropped), and with
  none at all through a null list; an expression ended by an unknown token at the end of the file moves the cursor
  before the start (the port checks); `<=` and `>=` are dropped from expressions (kept).
- **Mutations:** 1,032 mutants, 102 not building, 831 of 930 killed, 99 equivalent or out of reach (mutate_sc1.py).
- **Harness lessons (my mistakes):** heredoc edits collapsed backslashes twice (one probe applied nothing); the first
  mutations included the include lines and "%d" (the port's include handling and missing arguments: not comparable);
  the random-atom expressions drove the original into undefined behaviour whose damage showed in later cases (a
  grammar now); the mutated tokens' line scan made the mutation generator quadratic (25 ms a mutant); a mutation list
  generated past the file's end was nearly run stale (stopped, regenerated).

## Script part 2 (the mission script interpreter: `script.cpp`'s execution, `script_vm.cpp`'s script-level commands, Evaluate / ToString, 2026-11-22)

- **The build:** `script/vm_diff.cpp` (suite `script.interpreter`, data needed; `sc_shared.inc` holds what it shares
  with `sc_diff.cpp`): the port's `script.cpp`, `script_vm.cpp`, `script_functions.cpp` and the compiler against the
  original's, each program compiled by both (compared as in part 1), then run side by side: ticks of random lengths
  and the events (SendSignal, OnDeath, OnUse, OnHit, OnAlarm, OnAlarmDone, OnGlobalAlarm, OnCutscene, OnCutsceneDone,
  OnActionDone, Stop, Restart, OnExit; the mission's start, the restart flag, the role switched between them). After
  each: the script's flags, signals, delay, start point, call stack, instruction pointer, running / finished, the
  Whenever timer and entries, the handlers, every number and string, the 256 save-game values with a sentinel word on
  each side, the results of OnAlarmDone / OnCutscene / OnCutsceneDone and of every real command each side ran. Faked
  alike: the game commands (each answer, Advance and true, waiting or Advance and false, drawn from a sequence both
  sides read in the same order; the original's through a vtable whose ExecuteCommand slot is the fake's) and the
  game functions (values from that sequence; the original's CallFunction redirected, its real one called through a
  trampoline for _SignalReceived, _RandomInt and _LoadGameValue); rand seeded alike per action. Run for real: the flow
  (If / Else, blocks, GoTo, Gosub / Return, Delay, the handlers and Whenevers), Evaluate and ToString, SetWhenever,
  SetWheneverTime, Disable*, EnableSignal, SaveGameValue, EndScript and SendSignal on a global script, the debug
  commands, _SignalReceived, _RandomInt, _LoadGameValue. The rest of the game the interpreter reaches is link stubs
  (`vm_stubs.s`, written by `gen_vm_stubs.py`; a call ends the test: none happens). Not compared: the profiling time.
- **The tests:** every game script (5,347, 200 steps each); 20,000 random programs from a grammar (variables,
  expressions of one precedence class per level with parentheses, NOT, the functions, the save values in and out of
  range, If / Else, blocks with Whenevers inside, labels, GoTo, Gosub, Return, Delay, the script-level commands, game
  commands, handlers of every kind) of 200 steps; three crafted programs run on ticks alone (Gosub past its depth of
  100, the save values' bounds, string and frame operands, a string joined by a dropped operator). About 4.6 million
  steps in 2 min 20 s (quick: 18 s). Every line in this part's reach runs; not reached: FRM_SetAnimationDelayed's wait,
  the actor paths (Stop, SendSignal and EndScript on an AI human or meleeTracker), NotifyFrameHit, ScriptAssign, a
  frame's SendSignal (the frame commands are faked: the frames stay null), and Evaluate's <= / >= cases (dropped by
  the compiler).
- **Port bugs found:** none (0 failures from the first run).
- **The original's weaknesses (kept unless noted):** Evaluate divides unchecked: a zero divisor faults (the port
  answers 0), and INT_MIN / -1 faults in both; an expression whose operators the compiler dropped altogether
  ("a <= b") has children and no operator list, and Evaluate and ToString read the null list and fault (the port skips
  the operand). The test steps over these faults in the original (counted: none in the game scripts). Also seen: a
  binary '-' never compiles ("c - c" fails: read as a sign), and a quoted name in SetWheneverTime names only a
  Whenever declared with the quotes; every grouping of mixed precedence uses a freed node (part 1), so those programs
  are not run.
- **Mutations:** 632 mutants, 56 not building, 495 of 576 killed, 81 out of this part's reach or equivalent
  (mutate_sc2.py).
- **Harness lessons (my mistakes):** the first build crashed in the original's compiler (the role left a client's
  before the first compile); the first programs compiled one time in two (binary minus, quoted Whenever names); a
  heredoc edit failed its own assertion and applied nothing; the first comparison left ExecuteCommand's results out
  (12 mutants survived until they were logged).

## Script part 3 (the interpreter's game commands and functions: `script_vm.cpp` past the script-level commands, `script_functions.cpp`'s game functions, 2026-11-23)

- **The build:** `script/cmd_diff.cpp` (suite `script.commands`, data needed): one program with frame, number and string
  variables compiled on both sides; then each case builds the same fake world on each side and runs one command
  (C_Script::ExecuteCommand, the original's 0x00416e10) or one function (CallFunction, 0x0041e3f0) on each. The world:
  24 engine frames of every type the commands tell apart, in a hierarchy (a human model's holsters, hand and pack among
  them), turned and placed at random, visuals with meshes, dummies with boxes; the scene (an active camera, the primary
  sector), the driver; actors on about half the frames (humans with health, role, body frame and movement; doors with
  their key, motion and lock; radios, switches, photo, explosive and document places, static weapons, pikes and TS
  explosions with their states; animations, trains, cars with their vehicle, melee trackers with their controller,
  items, Junkers, wire cuts, air attacks, barrels), the actors module's list (32 player slots, then the rest), mines
  among the temporary actors; the teams, formations with their shapes, the checkpoints of both networks by name, the
  interface's hints, the player profile with its debriefing, the play mode, the script module's cutscene flags, the
  Junkers and decorators globals, the role (single player, dedicated or listen server), the mission's time (mostly
  started). Half the frames lie near the origin, and some under a dummy near its box's faces, so the boxes and ranges
  hold bodies and players; the box commands get a dummy often. Every call into the game or the
  engine goes to one logger on both sides (`cmd_calls.s` / `cmd_calls.inc`, written by `gen_cmd_calls.py`: the port's
  link stubs, the original's patched functions by address and calling convention, the engine's vtable slots from
  i3d.h, the game's virtual slots per side from member pointers and the headers) and is answered alike: from the world
  (a frame by name, children, the members, the list, the formation of a leader, the same shift entry of a mesh) or from
  a sequence both sides draw in the same order. Compared after each case: the answer, the script's flags, signals,
  delay, wait, ip, running / finished and every variable, every frame (reference count, flags, position, rotation,
  scale, parent), the actors (type, position, direction, health, the animation's hold, the train's run), the Junkers'
  interceptors, the game's flags, the hints, the cutscene flags, the shift entries, and the whole call log with every
  argument (strings, vectors, matrices by value; objects by name; the explosion, the sound request and the debris
  parameters by their fields; an animation set with its play mode).
- **The tests:** 200,000 cases (quick: 30,000, about 20 s), a quarter of them functions; 221 commands and 33 functions. Every
  line of the game commands and functions runs but the part-2 commands; not reached: none.
- **Port bugs found and fixed:** CSC_FadeIn/OutFromColor divided by 255 (the original multiplies by 1/255: a rounding
  apart); WEATHER_SetDir normalized with the square summed x, y, z (the original's inline copy: (z² + y²) + x²);
  FRM_CreatePhysicalObject and FRM_TeleportNearFrame took a direction with the inline sum where the original calls
  I3D_frame::GetWorldDir's own copy ((y² + z²) + x²); HUMAN_KillAllInBox's three random numbers drawn right to left (one
  constructor call: g++'s order; the original's x first); FRM_TeleportNearFrame asked for the sector at the target's
  position (the original's at the moved frame's, the same place in the engine, not in every case); SetActorState of a
  wire cut skipped the frame's direction when the state was 0 (the original takes it first: the world matrix is
  computed either way); and the calls made in another order or skipped: CSC_GetPlayerNum asked the index's sign before
  the alive count, FORMATION_AddMember looked the leader's formation up before checking the member, _IsTeamMemberDead
  asked the alive count before the member count. The unused IsActivePlayer helper was removed.
- **The original's weaknesses (kept unless noted):** FRM_CreateParticle sends a null frame's name (a crash; the port
  checks it, as before); CSC_SetCameraFOV uses a missing active camera, CSC_DuplicateHuman a frame the driver could not
  create, _ItemInRange, _FrameInRange, _CameraInRange, _GetDistToActivePlayer and the MP functions a script without a
  frame, _PlayerInBox a null box (crashes in both); SetPosition / SetDirection copy a fourth word from past the vector
  they get (a stack leftover; the port writes 1 and 0; not compared); FRM_CreatePhysicalObject leaves the debris parameters'
  first two vectors, the velocity's w, the context and the flag at +0x41 as the stack had them (Setup reads +0x20 on;
  the port writes zeros; the flag at +0x41 grows the resting volume, not compared). The test changes such inputs (counted).
- **Mutations:** 1,207 mutants, 153 not building; 1,017 of the 1,054 building killed, 37 survived
  (`mutate_sc3.py`'s docstring): float ties (the box's min / max, the strict insides, the ranges, the weather's
  clamps), equivalent ones (Clamp255's 0, a ninth box corner, a holster without children, PrintfNumber's buffer, the
  switch's default, no null actor past the player slots), the commands' bad variable indices (the compiler writes only
  valid ones), and two rare ones (corner 0 left out of a transformed box; a one-letter track on a train). The first
  quick run left 158; the test then logged the structures passed by address, put bodies and players in the boxes and
  ranges, set the mission's time (Advance moves the script only then: the commands that advance it themselves went
  unchecked before), seeded the cameras' range, the hints and the HUD flags, and drew numbers past the weather's clamps.
- **Harness lessons (my mistakes):** the FPU was left at its 53-bit default (the original's x87 sums rounded otherwise;
  the game runs at 24); frames created mid-case grew the frames vector and moved the short names m_name points into;
  CanPlayerRespawn is a member in the original (callee pops: the stack shifted, a crash two calls later); my first
  argument-order fix drew them in the wrong order (I read g++'s order as the original's); a fake DuplicateModel copied
  no children until the names were stable; two edits through Bash heredocs lost their backslashes again (a third failed its assert); the first test world never
  started the mission, so no command's Advance was compared, and never put a human inside a box or a range (the
  mutation run showed both).

## Script part 4 (the actor scripts: `actor_scripts.cpp`, C_HumanScript and C_MeleeTrackerScript, 2026-11-23)

- **The build:** part 3's `script/cmd_diff.cpp` (suite `script.commands`): half its cases run an actor script's command
  instead, on the port's C_HumanScript / C_MeleeTrackerScript (compiled from the same program) and on the original's
  ExecuteCommand (0x00421c00 / 0x00421970, the actor at +0x10C, the class's vtable put on the block); HD2_CMD_ACTORS runs
  only those. The world adds what they read: every human gets a controller (its type, its current checkpoint, the
  circumvention flag), its work, a vehicle and a seat (cars, static weapons, other actors, none); every actor a network
  id; bikes among the actors; a melee tracker on no frame with its controller (its mode: the full AI's 4 and 5 mostly,
  others) for the tracker script. The new calls (the human's commands, the melee tracker AI's, the AI controller's and the
  bike's Drive, the checkpoint lookups) are logged like part 3's: the human commands by their fields, GetNeighbours /
  GetReachable fill each side's own vector layout (the original's MSVC 6 vector through its operator new), FindNearest's
  vector by value as three floats, Get's checkpoint by its low half (the original reads AX alone and pushes a leftover
  upper half), FindCheckpoint by name. The compared state adds each human's sight and hearing ranges and its controller's
  circumvention flag (inline writes). The constructors (0x00421bc0 / 0x00421930) are compared once: the actor, the global
  flag, the actor flag.
- **The tests:** 200,000 cases (half of them the actor scripts'; HD2_CMD_ACTORS alone: 200,000, 0 failures). Most of an
  actor script's commands are its own (the human's 28..248, the tracker's 13..27), some fall to the base or are skipped.
  Biased for the rare paths: MoveToFrame / MoveFromFrame's target at the human's own place or a unit or 2^-15 m step from
  it (the direction's unit and near-zero rules), HUMAN_Bike's frame turned into a bike and its point empty, the drives'
  human in a car's driver seat, BoardVehicle's frame turned into a vehicle, the script's delay seeded. Every line of
  `actor_scripts.cpp` runs (253 of 253); not reached: the GetKind inlines of `actor_scripts.h`.
- **Port bugs found and fixed:** HUMAN_Move / AttackMove compared FindCheckpoint's answer with -1 (it answers 0xFFFF for
  a missing checkpoint, as the original's AX: the port moved to checkpoint 0xFFFF instead of skipping the command);
  HUMAN_Investigate and ACTIVITY_Phone / Read / Point played the activity without a frame when the frame was null (the
  original passes the null to PlayActivityAt; DrinkBottle and Eat do choose by the frame, as before); MoveToFrame /
  MoveFromFrame normalized the direction with the square summed x, y, z (the original's inline copy: (x² + z²) + y², the
  goal a float's last bit apart).
- **The original's weaknesses (kept):** the melee tracker commands call the full AI's functions on whatever controller the
  tracker has (HafHaf the scripted one's): the port skips them for the others, as before; HUMAN_Drive / DriveNearPlayer
  stopping the car drive through a player's or a remote controller too (the port only through an AI's). The test changes
  such inputs (counted).
- **Mutations:** 365 mutants (`mutate_sc4.py`), 34 not building; 318 of the 331 building killed, 13 survived: equivalent
  ones (the direction's unit test, the near-zero signs, the constructors' actor flag the port's base already clears,
  LookAround's 0, the skipped switch's value), rare ones (a square of exactly 1.5 or 1e-8, two players exactly as far) and
  an unreachable string index. The first run left 25: the test then compared the constructors, seeded the delay, put the
  drives' human in a car and BoardVehicle's frame on a vehicle often, and gave the checkpoints other flags.
- **Harness lessons (my mistakes):** the call table's answers had to come from the world (the controller's type, a
  checkpoint by name) where part 3 had drawn them; the original's checkpoint words carry a leftover upper half (logged as
  the low half); a Python heredoc lost backslashes again (twice); C: filled up during the mutation run (the test compiles
  failed with only warnings shown): the runs' temp files now go to T:.

## Script part 5 (the rest of `script.cpp`: FRM_SetAnimationDelayed's wait, C_Script's saved games, scripts made on frames, 2026-11-23)

- **The build:** part 2's `script/vm_diff.cpp` (suite `script.interpreter`) and part 3's `script/cmd_diff.cpp` (suite
  `script.commands`), extended.
  - vm_diff's programs get three more actions: the wait set up alike on both sides (the label mostly the instruction
    pointer's, a fake animated model with its time and end; the ticks then run it), SaveGame (each side's chunk file in
    memory, sc_shared.inc's files now writable: the result, the chunk's place after and the bytes compared) and
    LoadGame (the same bytes on both sides: the original's last save, or a file made for the script with its chunks in
    any order, repeated, missing, unknown, short or long, a broken name, a file cut short). Each side has its own fake
    frames and scene (FindFrame), fake items in the placements and the respawning list (C_Item::GetFrame faked on both
    sides) and a script kind (GetKind: the human's and the melee tracker's chunks); every call on them is logged and
    compared, with the frame variables, the references and the animation times. A crafted program runs with a command
    made null after its compile (Tick's check).
  - cmd_diff gets a case kind (one case in 12): a script made on a world frame (with the world's data, without data,
    with data and no actor; global or not), then nothing, Stop, SendSignal or NotifyFrameHit, then destroyed; compared:
    the frame's data after each (its script: this one, another, none), the script's state, the frames' references and
    the calls (SetFrameData's data by its fields; its call table entry was cdecl, the original's is fastcall: fixed).
- **The tests:** vm_diff full: the crafted programs, 20,000 random ones and the 5,347 game scripts (quick: 3,000 random
  ones, 60 steps each; about 25 s); cmd_diff: 200,000 cases (quick 30,000, about 2,600 of them scripts on frames).
  Every line of `script.cpp` now runs (both suites together); not reached: ReadString's closed-chunk guard
  (`chunk.cpp`).
- **Port bugs found and fixed:**
  - LoadGame's frame names: a name sub-chunk that cannot be entered ended the original's load (false, "Broken chunk",
    nothing ascended); the port skipped it and went on.
  - LoadGame's numbers: the original reads every number variable through one value (0 before the first): a variable
    the chunk has no value left for gets the last one read; the port left it as it was.
  - C_chunk::ReadString (`chunk.cpp`, every reader of a chunk's remaining string): a chunk cut short by the file's end
    kept the bytes read in the original (0x00415d00); the port's string was empty.
  - Tick's wait asked the model for its time before the animation (the original: the animation first); no effect with
    the engine's getters, now in the original's order.
  - Clear released the frame variables first to last; the original empties every list last to first (the frames'
    releases are the engine's calls, so their order shows).
- **The original's weaknesses (not tested, the port's behaviour kept):** its frame-name reader copies the chunk's bytes
  into a stack buffer without a terminator: a name without one, a chunk shorter than its name, the next chunk's bytes
  read as a name (more frame variables than names) or a broken name at the file's end leave it reading its stale stack;
  ReadSizedString's text shorter or longer than its length (or past the chunk) does the same, and one longer than the
  buffer (0x400) is skipped by reading the rest onto its stack; a name chunk shorter than a header reads a negative size.
  LoadGame reads the instruction pointer, the timer, the delay and the two signal masks into a stack word it stores
  whether the read worked or not: a file cut short there gives them its stale stack (the port keeps their values). The
  made files avoid these. C_Script's constructor leaves the frame data's last word (+0x1C) as its stack had it (the
  port's is 0; nothing reads it).
- **Mutations:** 266 mutants (`mutate_sc5.py`: script.cpp's part-5 lines and ReadString), 27 not building; 214 of the
  239 building killed (vm_diff first, the survivors on cmd_diff). The first runs left 31: the tests then compared the
  fakes after the scripts are freed (the releases' order: a port difference, above), gave the harness's
  g_noPhysicsObject a value, wrote one-letter names, an empty call stack chunk and stray bytes at the chunk's end.
  Survivors: equivalent ones (FreeArgs' frees, an empty name set on an empty C_str, the destructor's last store, a
  larger name buffer) and ReadString's lines a script's load cannot tell (its 999-byte limit, the negative size the
  files avoid, a closed chunk, a failed read, the terminator after names that carry one, its unused result).
- **Harness lessons (my mistakes):** the first build crashed in the test itself (SetFrameData decoded as cdecl: part 3
  never reached it); I started one build with a trailing `&` (it failed at once); the corpus run (the game scripts)
  found the broken-name bug the quick random runs missed, so mutation baselines run with the corpus.

## Script part 6 (the script module's saved games, a joining client's state and the clients' messages: `script_module_save.cpp`, 2026-11-23)

- **The build:** `script/build_mod.bat` (`mod_diff.cpp`, suite `script.module`), new. It includes `cmd_diff.cpp` for
  its world, fakes and call logger (its main and call table switched off by macros); the module's own calls go through
  `mod_calls.s` / `mod_calls.inc` (`gen_cmd_calls.py mod`: the generator now takes the test's name and redirects the
  original's functions only where the port does not link them). `script_module.cpp`, `script_module_save.cpp`,
  `module.cpp` and `resource_cache.cpp` are linked: the module's own functions and the animation cache run on both sides
  (the original's LoadGame inlines the cache's Get). The original's module and animation cache are constructed by hand
  (their static initialisers never ran; the cache's std::_Lockit critical section is set up first, as the original's CRT
  never started).
  - Each case builds the world, then gives each side's module the same state: scripts compiled on world frames (the
    original's in blocks of its operator new), pause sounds, rotations and remembered frames (the original's in blocks of
    its _aligned_malloc, redirected to the CRT's), sound frames, saved values, the cutscene flags, lightmap groups, the
    models' file names, the humans' movements and actors of chosen kinds on the models.
    - The lightmap groups use the original's layout on both sides: the port's FindGroup is answered by the same search,
      unlogged, as the original inlines it.
    - The movements are blocks large enough for both layouts: the shadow frame at the original's +0xA9C and in the
      port's member.
  - Then it runs one operation and compares: the answer, the module's lists and fields, the pending sounds, the frames,
    the animation sets' reference counts, the chunk file and every call.
- **The tests:**
  - SaveGame.
  - LoadGame on the original's last save or on a made file, its chunks in any order: values, pause sounds, rotations
    (their sub-chunks in any order or missing), weather, the fake cutscene flag, an unknown chunk, remembered frames
    with every part (names not found, parents, sounds, animations by the model's own file or the cache's) and scripts
    (the original's saved ones: as saved, on another frame or a gone one, a file that does not compile, a sub-chunk
    missing or swapped). Half the module's scripts run a program with handlers (OnExit, OnGlobalAlarm, the cutscenes')
    that write saved values.
  - WriteState.
  - ReadState and OnMessage: every message type, with the ReadBits and ReadString answers from the case (names,
    signalling NaNs); now and then no message at all. The debug texts both sides print (OutputDebugStringA's exception,
    caught) are compared as calls.
  - 20,000 cases, about 21 s (quick: 4,000, 4 s).
- **Coverage:** every line but one: LoadModelAnimations' check for a frame that is not a model. A load reaches it only
  after calling SetAnimation through another frame type's vtable; the game's own saves never ask for it.
- **Port bugs found and fixed:**
  - LoadModelAnimations: when the animation cache did not give a file, the port skipped the whole entry; the original
    still stops the stage when it was saved inactive. (The `script_module` unit test expected the old skip: its
    TestLoad now wants the stop.)
  - ReadState and the speed and size messages: the original copies the float weather values through the FPU (ReadState's
    speed to height and the direction, the messages' speed, length and width), so a signalling NaN arrives quiet; the
    port passed the bits as read.
  - WriteState's debug text lacked the original's newline.
- **The original's weaknesses (not tested, the port's behaviour kept):**
  - A rotation chunk without its quaternion sets the frame's rotation from the stack's leftovers (the port's from its
    own).
  - An unknown sub-chunk in a rotation chunk is not ascended past: its data are read as the next header (a made file hung
    the original).
  - A weather chunk shorter than its 10 values leaves the rest of the original's from its stack (the port's are 0).
  - A script compiled without the server's role (a pure client's AssignScript): the original's AddCommand adds nothing,
    and a brace block's end then reads the command before the first: a crash. (The cases compile as the server; a
    load is always the server's, as in the game.)
  - ReadState's sound count is signed: a negative one counts down through 2^32 on both sides.
  - The velocity and volume of a remembered frame that is not a sound are its allocation's leftovers on both sides (not
    compared; the port's volume is 0).
- **Harness decisions:**
  - When its vector grows, the original's animation cache copies its entries with an inline AddRef and releases the
    old copies: the sets' Release calls are not logged; their final counts are compared.
  - AssignScript's GetControllerType (the original reads it inline) is answered unlogged.
- **Mutations:** 578 mutants (`mutate_sc6.py`), 83 not building; 477 of the 495 building killed. The first run left 62
  (see the lessons below); the 18 left: the values a failed read leaves (the files are never cut short), the name
  buffers a byte larger, leaks, a pop_back the clear after covers, and the port's stop at a short chunk header (the
  original goes on with the last chunk's id; not tested).
- **Harness lessons (my mistakes):**
  - The saved files were compared through F(), whose 1,200 characters cut most of each file off: 62 mutants survived the
    first run, among them every changed chunk id of SaveGame. vm_diff's part 5 save comparison had the same flaw
    (fixed; it still passes).
  - The original's compiler crashed in a few cases: I chased a frame, the global flag and the frees before seeing the
    case's role was set before the compile (a client's role: no commands added).
  - The build script runs the full test after building: development builds now set HD2_MOD_CASES=0.
  - The heredoc backslashes again (build_mod.bat).
  - A module state the game cannot make: a sound bit on a camera. The original's call through the camera's vtable slot
    unbalanced its stack and crashed it.
  - `sed -i` on the new test left it with LF line endings (set back to CRLF).

## Script part 7 (the script module's other functions: `script_module.cpp`, 2026-11-23)

- **The build:** part 6's `mod_diff.cpp` (suite `script.module`) extended. `gen_cmd_calls.py` now reads `igraph.h`'s
  IGraph slots too, and GetIGraph is a logged call (the original's import thunk 0x007e530e patched). The original's
  model cache (0x008aeda0, its initialiser 0x006d51e0) is constructed by hand like part 6's animation cache, with one
  directory on both sides: StopMission reloads a model through its OpenInto, which both sides run.
  - Each case also gives both modules: started, the input delay, the command map flag, the cutscene's camera
    (referenced), its saved aspect and FOV, the cutscene number, the animated models with their cutscene flags, the
    duplicates, the script list (the original's C_str pairs made by its own operator=), the values at the start, the
    pending sounds (null entries among them), the frames' owner models, the screen (IGraph's ScrnX, ScrnY), the
    scene's viewport, the game's cutscene FOV and input flag, and the scripts' HUD and cutscene-handler flags.
  - EnumFrames is answered by calling the callback (each side's own) on the frame's descendants, so StopMission's
    model reload detaches and relinks real children.
  - Compared as well as part 6's: the frames' local matrices (a rotation turns them in place), the script list, the
    game's two fields and g_scriptModuleRestarting.
- **The tests:** every function of the file: RememberFrame, RestoreFrame, RegisterSoundFrame, (Un)RegisterAnimatedModel,
  (Un)RegisterDuplicate, RotateFrame, Update, Tick, LoadScriptList (a made list, the game's AFRICA1 list, none),
  Start/Stop/Restart/FreeMissionData, AssignScript (with and without `.scr`, other cases, empty, null, missing files),
  BroadcastGlobalAlarm, AddPauseSound, SetPauseSoundsPaused, RunCutscene, EndCutscene, Write/ReadSaveGameValues (short
  and long chunks), BeginScriptNetMessage and SendScriptNetMessage. 40,000 cases (part 6's operations about a third of
  them), about 48 s (quick: 8,000, 11 s).
- **Coverage:** every line but two: the module's destructor (a static object: the test never runs it), and
  RestoreAnimatedModel's first loop over the relinked frames (the call before always empties the list; the original's
  inlined copy has the same loop). `script_tables_lookup.cpp`: every line covered by `script.compiler`
  (GetScriptFunctionArgCount only there: the module's scripts call no functions).
- **Port bugs found and fixed:**
  - RunCutscene sized the 16:9 band by the screen (IGraph); the original's inlined copy reads the scene's viewport
    (its right and bottom, +0x24C / +0x250). Update's copy does ask IGraph, as the port did.
  - StopMission freed the rotations and restored the remembered frames first to last; the original goes last to first
    (each frame's calls and Release in that order) and skips null entries.
  - EndCutscene asks IGraph for the height before the width (the port's order was the compiler's).
- **The original's weaknesses (not tested, the port's behaviour kept):**
  - LoadScriptList: an entry without its script chunk gets the original's stack buffer's leftovers (the last name it
    read) and the list's chunk is left there; names of 999 characters or more are read unterminated.
  - AssignScript writes the path with sprintf into a 0x100 buffer: a long script name overflows it (the port
    truncates).
  - AssignScript (and so StartMission) reads each script's frame unchecked when it looks for the frame's: a null entry
    crashes both sides (not in the cases where it runs).
  - RunCutscene's band from the viewport: a cutscene started while one runs narrows the band again (kept: it is the
    original's).
  - RunCutscene's copy protection (the window handle passed through a pointer in the `.dogui` section) is left out of
    the port; the test stubs it.
- **Harness decisions:** GetIGraph and the protection's GetHWND unlogged (plain getters / the protection);
  EnumFrames logged without its callback (each side's own function); the original's model cache's entry vector made
  large enough never to grow (grown, it copies its entries with an inline AddRef and releases the old copies, as part
  6's animation cache).
- **Mutations:** 540 mutants (`mutate_sc7.py`), 42 not building; 468 of the 498 building killed. The first run left 39:
  the humans' scripted controllers' script was not compared (AssignScript gives an AI human's controller the new
  script and clears it when the file fails), and the rotation's and the input delay's exact edges never came up. The
  30 left: frees, the name buffers' sizes, code the test never runs (a static constructor, a dedicated-server line),
  LoadScriptList's loop test and closing calls, two clears covering each other, a new parent the test's model reload
  cannot change, and RestartMission's flag and client calls (no-ops in this file).
- **Harness lessons (my mistakes):**
  - The first mutation run failed at once: I wrote the mutant list outside the mutations folder, and the list finds
    the repository from its own place.
  - The frames' dump compared position, quaternion and scale only: Update's turned matrices were not compared until I
    looked at a case's output.
  - To show the controller's script being cleared, I first gave the controllers one of the module's scripts: a freed
    script's address came back to a new one on one side only, and RestartMission failed now and then. The controllers
    now start with a block that is never freed.

## Camera part 1 (the camera module's core and the vehicles' camera keeper: `camera_module.cpp`, 2026-11-23)

- **The build:** `script/cam_diff.cpp` (suite `camera.module`), over `cmd_diff.cpp`'s world, fakes and call logger as
  `mod_diff.cpp`; the module's calls logged through `cam_calls.s` / `cam_calls.inc` (`gen_cmd_calls.py cam`). The
  original's module (0x0089afb0) is constructed by its own constructor (0x00464ef0) in a world of its own. The module's
  layout is the original's (every field's offset checked), so each case gives both modules the same bytes, the pointers
  each side's world frames (the game camera, the scene's camera, the camera dummy, the sector, a volume, the target, the
  scripted view), the human effects' fields and cameras, the game's FOV, clips, time, pause and input flags, its
  first-person view's flags, the scene's active camera and a fixed active player (unlogged: the original asks for it
  twice where the port asks once).
  - LS3DF's math the module uses (the quaternions, AngleTo, RotateByMatrix, `*=`) is LS3DF's own on both sides; the
    debug texts are logged on both (the original's dbgPrintf thunk redirected); the original's file seeks and writes
    go to the test's files as the reads do.
  - Compared: the answer, the module's bytes (+0x10 on; its name and number), the human effects' bytes, the frames and
    their matrices, the keeper's bytes after each step, and every call.
- **The tests:** StartMission (a server, a client, the scene without a camera, the settings file the game's, made or
  missing), StopMission (tactical: the debug camera on so that SelectView returns; a camera dummy in a human's model),
  SaveGame, LoadSettings (made files: short, versions other than 2, bytes past the version, records cut anywhere),
  ResetUnknown2AC, GetCameraPos / GetCameraDir (every index, no scene camera), SetBlendTime, SetTarget,
  GetViewDistance, the fields of view, SetViewDir, IsGameCamera, the getters and GetCameraModule; the keeper: its
  constructor, Update, Restore (its parent moved between) and destructor on a car, a submarine, another actor or none,
  the player riding it or not, the ground hit or not, its dummy looking straight up or down, lined up with its vehicle
  (its view's y exactly 0.3) or nearly, the vehicle barely moving. 40,000 cases, about 54 s (quick: 8,000, 11 s).
- **Coverage:** every line of part 1's functions but the static module's and its human effects' destructors (never run
  in the test). RestartMission and LoadGame wait for part 4 (they run SelectView and Update).
- **Port bug found and fixed:** the keeper's "vehicle camera speed" (the original's 0x0089b208) is no global: it is the
  module's own turn speed (0x0089afb0 + 0x258). The active soldier's vehicle's keeper sets it every frame, and the
  passenger's view (7) turns at it; the port wrote an unread global, so that view always turned at 6. The keeper now
  sets the module's field (`vehicle_setup/camera_test.inc` checks it there).
- **The original's weaknesses (not tested):** the keeper's constructor and Restore read the dummy's parent unchecked (a
  dummy at the root crashes both sides; the game's always have one).
- **Harness decisions:** the debug camera on in the tactical StopMission cases (SetTactical(false) then runs SelectView,
  part 4's); the active player unlogged; GetGame's first-person view always there (the game always has one).
- **Mutations:** 366 mutants (`mutate_cam1.py`), 39 not building; 273 of the 327 building killed. The 54 left: the
  layout's static_asserts, the static destructor, RestartMission and LoadGame (part 4), NormalizeSummed's equivalent
  edges, LoadSettings' version start value and closing calls, a submarine speed of exactly 0.03.
- **Harness lessons (my mistakes):**
  - The first build ended with `_exit` (the static modules' destructors would release freed frames): the coverage
    counts were never written. The port's human effects now get back a camera that is safe to release, and the test
    returns normally.
  - The original's dtaSeek was not redirected to the test's files: its seeks did nothing, and LoadSettings agreed only
    on well-formed files. A made file with bytes past the version showed it.
  - The port's game fields, actor direction and submarine fields were first written at the original's offsets: the
    port's g++ layout has them elsewhere. They are written through the port's members now.

## Camera part 2 (the command map's tactical camera: `camera_module.cpp`'s C_CameraHumanEffects, 2026-11-24)

- **The build:** part 1's `script/cam_diff.cpp` (suite `camera.module`) extended; the human effects' functions are
  called on the module's own object (+0x0C) on both sides, the private ones directly. Each case also gives both sides
  the faded humans (the original's MSVC 6 vector first +0x8C, last +0x90, end +0x94, room enough never to grow; the
  port's std::vector), the player team's known enemies (the original's records first +0x3C, last +0x40; the port's
  vector at +0x38), the human argument's vehicle (the world's, none, or a made car, submarine, static weapon,
  Junkers, human or bike), the humans' view direction (GetViewDir), the effects' quaternions unit now and then.
  - Answered: CollisionSphereTest and CollisionSlideSphere hit now and then part of the way along the move (a plain
    volume or a water surface's; the slide's end a little aside), SegmentDistance2 among values on and around
    GetTransparency's 0.4 and 2.4 edges, the scene's GetSector the primary sector or another.
  - Compared as in part 1, the faded humans listed with their transparency.
- **The tests:** SetTacticalZoom, IsTactical, SetTacticalMotion, StartFlight (the camera at small integers and the
  point an axis' length away: 1, 5, 34.5, 35 m and around; tiny ways), UpdateLookPoint, LookAt (a human in each kind
  of vehicle, none: the game camera's), SetTactical on and off, Remove, SetTacticalMode (every mode from every mode,
  the flight's 300 ms drag edge, double clicks), ResetHumans, GetTransparency, UpdateHuman, UpdateHumans,
  UpdateTactical (every mode; the double click's time running out exactly; the flight's thirds' edges), Move.
  40,000 cases with part 1's (HD2_CAM_PART=2: part 2's only; quick 8,000, about 11 s).
- **Coverage:** every line of part 2's functions but the static human effects' destructor (never run in the test).
- **Port bugs found:** none.
- **The original's weaknesses (kept):** StartFlight, LookAt, UpdateTactical and Move write the point's and the camera
  position's w (+0x0C, +0x2C) from uninitialised locals (never read; the port keeps its own values, and the test
  leaves those two words out). GetTransparency, UpdateHuman and SetTactical(on) read the human / the active player
  unchecked (both sides crash on null; the test does not pass one).
- **Harness decisions:** SetTactical(off)'s SelectView(0) is part 4's: those cases have the debug camera on, so it
  returns at once.
- **Mutations:** 441 mutants (`mutate_cam2.py`), 36 not building; 388 of the 405 building killed. The 17 left:
  equivalent ones (the same float, sqrt(1), SetDir's whole quaternion, ResetHumans(true), the transparency's edges,
  LS3DF's Slerp ignoring its flag), equalities the cases never reach (the 89-degree limit, a square 6e-8 short of 1),
  and SelectView (part 4).
- **Harness lessons (my mistakes):** none new. The first run's edges (34 to 35 m, the distances just past 0.4 and
  2.4, the click timer at exactly 0) were added after the mutation run.

## Camera part 3 (the camera's update: `camera_module.cpp`'s Update, the car's, the scripted and the debug cameras, PlaceCamera, MoveCamera, 2026-11-24)

- **The build:** `script/cam_diff.cpp` (suite `camera.module`) extended again; Update, UpdateCarCamera,
  UpdateScriptedView, UpdateDebugCamera, PlaceCamera and MoveCamera (with UpdateShake, TurnToTarget and
  PlaceTurretCamera, inline in the original's MoveCamera) called on both modules with the case's time step. Each case
  also gives both sides the mouse's deltas and buttons (a made IGraph), the options' inverted mouse, the controls' look
  and debug speed values, the selected member, the active player's vehicle (a car mostly, another kind, none: a made
  actor with the world's actor vtable) and seat, the seat's flags and first-person-only answer (the seat virtuals are new
  in `cmd_diff.cpp`'s table: GetSeatFlags, GetSeatCamera, ShowSeatView, IsSeatFirstPersonOnly), the head under water
  and its surface, the camera dummy's roll, the aiming sway (the original's 0x00839268; the port's `g_cameraAimSway`,
  out of its anonymous namespace for the test), the water level near the dummy's height now and then.
  - Answered: CollisionSphereTest hits part of the way now and then (a plain volume, a water surface's, a dynamic one
    whose frame is the camera dummy or another), C_CollisionWorld::RayTest likewise with a normal, the scene's GetSector.
  - The original's PoseGetRoll is __fastcall with the roll written through EDX (the port's returns it): the original's
    is replaced by a stand-in writing the case's roll; the call is not logged.
  - Compared as in parts 1 and 2, plus the aiming sway; the directions' and places' w the original writes from
    uninitialised locals (+0x2C, +0x3C, +0x4C, +0x6C, +0x7C) are left out in part 3's cases.
- **The tests:** Update on every path (the debug camera, the spectators', the scripted view, the car, the main one:
  first person under and over the water's band, the mouse's yaw and pitch at the view's limits, the camera dummy's own
  direction, the listener's sector every 500 ms, the offsets shrinking, the aim time at its 5 s limit), the car's camera
  (every seat kind, the look limits, the heading's and elevation's wraps), the scripted view (the shake running out),
  the debug camera (the buttons, the speed steps at both ends), PlaceCamera (near +z, the plain and the swept
  placement, the turret's arm, hits on the dummy's own model, under water, the blend both ways, the cut) and
  MoveCamera (the target, the aiming sway with the breath held or not, the turret's view, the soldier fading).
  40,000 cases with parts 1 and 2 (HD2_CAM_PART=3: part 3's only; quick 8,000, about 10 s).
- **Coverage:** every line of part 3's functions.
- **Port bugs found and fixed:**
  - The aiming sway's rates are the original's doubles 5.000000237e-06 and 4.000000190e-06 per ms (0x0080fef0,
    0x0080fee0), not 5e-06 and 4e-06: the sway was a bit off now and then. The original rounds the step times the rate
    once to single precision; the port's double product does too in the game (x87 at 24 bits), but rounds twice where
    doubles keep 53 bits (the SSE harness): `ProductToFloat` breaks those midpoints with the exact remainder.
  - PlaceCamera and TurnToTarget divide by AngleTo's answer as the FPU holds it: its FPATAN is not rounded to single
    precision. The port rounded it to a float first, so the turn's slerp fraction, and the camera's direction, was a
    bit off now and then. `AngleToUnrounded` (src/ls3d/math.h) reads the same LS3DF function's ST0 as a double.
- **The original's weaknesses (kept):** UpdateCarCamera reads the active player and his vehicle unchecked (Update only
  calls it with both); the update's paths read the game camera unchecked, the car's and view 2's under water the camera
  dummy, view 8 the dummy's parent (the turret's arm). The test does not pass those nulls.
- **Harness decisions:** the debug speed action's two calls answered in order (the first, the second); PoseGetRoll's
  matrix not checked (the camera dummy's matrix is compared through the frames).
- **Mutations:** 987 mutants (`mutate_cam3.py`), 71 not building; 843 of the 916 building killed. The 73 left: clamps whose
  equality sets the same value, the side axis' zero y, LS3DF's Slerp flag, the server's `#else`, float equalities
  never reached (angles exactly pi, aside exactly 0.08), narrow bands (an offset's delay ending 1 ms after the step,
  aside between 0.08 and 0.081) and ProductToFloat's split (only midpoint steps reach it; the helper was checked
  against exact arithmetic for every step within 200 s apart from the test). The first run's 89 found the gaps the
  cases now cover (the fade's 0.2 and 1.2 edges, 1 ms shake and blend times, the look limits' last hundredths, the
  water band under the surface, the sway's midpoint steps).
- **Harness lessons (my mistakes):** part 1's call table had PoseGetRoll as a cdecl float answer; the original's is
  __fastcall writing through EDX (found when its result stayed uninitialised). The rounding hunt went through
  precision modes first; the constants' bytes (Ghidra shows them rounded) were the answer. ProductToFloat's first
  version took the remainder from std::fma: MinGW's runs on the x87 (at 24 bits in the test), so it was wrong.

## Camera part 4 (the views and the special modes: `camera_module.cpp`'s SelectView, SetSpecialMode, RestartMission, LoadGame, 2026-11-24)

- **The build:** `script/cam_diff.cpp` (suite `camera.module`) extended once more; SelectView, SetSpecialMode, RestartMission
  and LoadGame called on both modules. The original's SetSpecialMode is one function taking its two arguments as raw
  words; the port's four overloads are called with what its callers pass (a frame, an int, a float's bits, a vector and
  a word), the original with the same words. Each case also gives both sides part 3's state (LoadGame updates), the
  game's role (the original's 0x00982100; the port's `g_gameRole`; set back after the case), the seat's camera dummy
  (GetSeatCamera), a car's seats (the original's array at +0x1038, 0x40 bytes each; the port's std::vector, its seats
  g++'s size: the port's C_Car layout under g++ is not the original's) with the seat's camera keeper or none, the car's
  camera offset, the Junkers' gun field of view, the player without movement now and then, the sector the player is in
  (none, the primary one, another) with user properties ("nointerier", "interierclose", either case, both, none), the
  frame the wildcard FindChildFrame finds ("camera3RD*", "cameraFPV*"), the spectators' cameras ("spectator",
  "spectator<n>").
  - Compared as in part 3; the wanted place's w (+0x1C), the original's uninitialised local in SelectView, is left out.
- **The tests:** SelectView on foot (outdoors, indoors, "interierclose", under water, first person, the binoculars, a
  client's), in every kind of seat (a weapon seat in the Junkers by an AI-held gunner or not, first person or third, a
  passenger in a car with its own camera keeper or not, armed, first person only), without a camera dummy, with the
  debug camera on or no game camera; a new camera dummy (the old one's soldier no longer faded) or the same; the blend
  reset. SetSpecialMode in every mode and past them (the debug camera with the spectators', the explosion's radius
  with the scripted view, the spectators counted and picked at random, the mouse look, under water, the kicks at their
  limits, aiming, the next spectator wrapping, the scripted view held and let go), each role. RestartMission (a
  client's or not, the player in the Junkers or not) and LoadGame. 40,000 cases with parts 1 to 3 (HD2_CAM_PART=4:
  part 4's only; quick 8,000, about 10 s).
- **Coverage:** every line of part 4's functions.
- **Port bugs found:** none.
- **The original's weaknesses (kept):** the explosion mode reads the game camera unchecked (the test gives it one);
  modes 1, 5 and 6 read their vector unchecked; SelectView reads the camera dummy it chose unchecked only where it
  cannot be null. Under water (mode 4) the original stores any overload's word as the water level; the port's callers
  all use the float overload, which passes the same bits (the test uses only that one there).
- **Harness decisions:** mode 4 only through the float overload; mode 9 only through the frame overload (the
  original stores the word as the scripted view's frame); modes 1, 5 and 6 with a vector.
- **Mutations:** 257 mutants (`mutate_cam4.py`), 20 not building; 219 of the 237 building killed. The 18 left:
  equivalent ones (SelectView's unused argument, the blend's sign, the frame overload's unused number, the kicks'
  redundant tests near 1 and under 1e-7), SaveGame's answer (part 1's), and the spectator pick's constant (a narrow
  band). The first run's gaps (the explosion at 2.4 to 2.5 radii with the scripted view, exactly at the radius, a
  kick just past 0.25) are now in the cases: the explosion is placed at set multiples of the radius from a game
  camera moved to (1, 2, 3).
- **Harness lessons (my mistakes):** the seats were first written at the original's offsets on both sides; g++ lays
  out the port's C_Car differently (the seat vector at +0xFC4, 0x3C-byte seats), so the port side now writes its own
  members.

## Net part 1 (the bit streams and the messages' encodings, `net/bitstream.cpp`, 2026-11-24)

The network code's older suites (net.replication, net.session, net.messages, net.commands, net.game, net.console,
net.firewall, net.gamespy) run about 90 % of its lines, but they check expectations written from the decompile the port
came from. From this part on the network code is compared with the original executable.

- **The build:** `net/build_bits.bat` (`net/bits_diff.cpp`, suite `net.bits`): bitstream.cpp alone against the
  original's functions, called on objects in the original's layout (its constructors run on them). The original's
  malloc, realloc and free are redirected, the port's wrapped (`--wrap`); both log their calls and fill new memory
  alike, each block with 64 bytes of slack. One player table is set in both programs' globals (0x00982dec, 0x00982df0).
  The original's packet Write and Read (0x00745fa0, 0x00746050, its pool's AllocBuffer faked) stand in for
  WriteRawBytes and ReadRawBytes, which the original has only inline there; WritePosition and ReadPosition likewise
  against WriteQuantized and ReadQuantized with GetSender(-1).
- **The tests:** rounds of up to 30 random calls on a pair of streams: a fixed buffer's (constructed with it, or by
  default and attached) or a growing message (with its sender and recipient by pointer or index; now and then attached
  to a buffer it does not own). After each call the fields, the buffer, the results and the allocations are compared.
  WriteBits (0 to 32 bits, values out of range too), ReadBits, WriteString (high characters too), ReadString (sizes 1
  to 48), SetBitPosition and ResetPosition with ByteSize and IsValid, Reserve (either class; the first block's edges at
  768 to 900 bits; growth by doubling or by the request), the packets' raw bytes at any bit position, Setup (spans and
  steps at exact powers of two and a float either side; past 32 bits and below 1, compared too), WriteQuantized and
  ReadQuantized (any mask; values at the dead zone, the minimum and the range's ends, NaN, out of range), Contains and
  WithinSteps, the three direction encodings both ways (axis-aligned, vertical and zero directions too), GetSender and
  GetRecipient, the destructor. 200,000 rounds, about 9 million checks, 10 s (HD2_BITS_QUICK: 40,000).
- **Coverage:** every line of bitstream.cpp.
- **Port bugs found and fixed:**
  - Setup's bits: the port worked the logarithms out in float, so a span an exact power of two times its step came
    out one bit short (8192 steps: 13 bits, not the original's 14), its scale half the original's. The original's FYL2X
    is not rounded by the game's single precision; only the quotient is. `net_quantization`'s model had the same
    mistake (its test was changed with it).
  - ReadQuantized: a value of more than 24 bits was rounded to float before the division (the original divides the
    exact integer once).
  - The direction encodings: the angles, sines and cosines were rounded to float before use, and the 3-D ones
    multiplied rounded cosines; the original keeps the x87's precision until it stores them.
  - WriteQuantized: a NaN y was replaced by the minimum and sent; the original keeps it and sends it as absent.
  - Contains: a NaN x or y failed; the original's compares pass it.
  - Reserve: a first request of 769 to 800 bits gave a capacity of the request plus 32; the original's is 800.
- **The original's weaknesses (kept):**
  - A growing stream's first block is 100 bytes whatever the capacity: a first request over 800 bits overflows it.
    The port sizes the block to the capacity; this was already the port's, and the test allows that difference.
  - Reserve reallocates capacity / 8 bytes: a capacity that is not a multiple of 32 (after a request bigger than the
    capacity) lets the last word's write run up to 3 bytes past the block. The port is the same.
  - Setup reads its mask from outside the table (0x00816388) for a component under 1 bit or over 32: the data next to
    it. The port uses none or all ones; no quantisation of the game's gets there. Not compared.
  - WriteString writes each character sign-extended: a high character sets stray bits past the stream's end (later
    writes clear them; never read back). The port masks it; the test compares the bits up to the end.
  - ReadString with a size under 1 would read one character and write before the buffer; every caller passes a size.
- **Mutations:** 233 mutants (`net/mutations/mutate_net1.py`), 22 not building; 193 of the 211 building killed. The
  18 left are equivalent: array sizes one larger, the static_asserts, a clamp at its own value, BitMask(0) for none,
  GetSender(-2) for -1.
- **Harness lessons (my mistakes):** the first runs compared Setup against the port's model of the x87, which an
  earlier part had written from the decompile; only the original settled it. The allocation logger first allocated
  through the wrapped malloc itself (its map), logging its own blocks.

## Net part 2 (the replicated values, `net/net_track.cpp`, 2026-11-24)

- **The build:** `net/build_track.bat` (`net/track_diff.cpp`, suite `net.track`): net_track.cpp and bitstream.cpp
  against the original's. Each round builds a pair of tracks (the track, the position track or the direction track;
  the original's by its constructor in a buffer of its layout, the port's layout checked to be the same) and a pair of
  messages over the test's buffers. The calls go through the original's vtable slots. The test's quantisations are
  shared; the direction tracks' are each program's globals (0x00982f10, 0x00982e90, 0x00982e10), compared after the
  first construction. The globals (the update period 0x00982f90, the server side 0x00982f94) are set on both sides.
- **The tests:** rounds of up to 40 random calls: SetFlags (the rate flags, any bits), SetLag, SetValue, SetNetValue,
  SetRate (zero, tiny, at 1e-16, Magnitude's floor, a unit, any), the getters and Refresh, GetAngle, Update (0 s, the
  tick, long), Write (delta or not; the last sent near the value: within its steps, its delta range, anywhere, or a
  step whose derived rate is about the 0.1 cut-off; the server side's net value), Read (from where an earlier Write
  began, or any bits), Reset, the quantisations (null ones kept), the server side switched, SetDirection (vertical,
  axis-aligned, any), SetAngle, the direction track's EndWrite and EndRead alone. Fields no call sets are written on
  both sides now and then: the snap distance (past half the angles' span, or at the distance itself), the raw flags
  (the direction track's moving target), the direction track's angles (turns of exactly 0.001, distances of exactly
  1e-8, 0.8, 0.785 and half the span). After every call the tracks' bytes, the messages and the globals are compared.
  100,000 rounds, about 4.5 million checks (HD2_TRACK_QUICK: 50,000).
- **Coverage:** every line of net_track.cpp but EndRead's tiny-rate branch (lines 350-355): the speed is over 8 there,
  so the square is over 64 (the original's dead code too).
- **Port bugs found and fixed:**
  - SetDirection's pitch: the port's asin was a correct one; the original's is its CRT's `_CIasin`, which works it out
    as atan2(x, sqrt((1 + x)(1 - x))) with the sum, difference, product and root rounded to float in the game's
    precision. Now `GameAsin` in `ls3d/math.h`, also used by part 1's direction encodings (their pitch quantised, so
    part 1 did not see it).
  - SetDirection's yaw and Update's direction: the angles, sines and cosines were rounded to float before use and the
    products multiplied rounded cosines (as part 1's encodings).
- **The original's weaknesses (kept):**
  - The constructors leave the vectors' w, +0x1C and the direction track's turn sign as the memory was: EndRead's first
    sign of "unchanged" then compares an uninitialised turn sign. The port's are 0 (not compared at construction).
  - The direction track's Reset drops its quantisations (its constructor sets them after); Update, Write and Read then
    read through null. The game resets them only in the constructor (the test does not update one after a Reset).
  - Update's step and EndRead's rate leave their w as an uninitialised local (not compared there).
- **Mutations:** 472 mutants (`net/mutations/mutate_net2.py`), 46 not building; 374 of the 426 building killed. The 52
  left: 15 in the unreachable branch, 34 equivalent, 3 narrow (exact squares of quantised values; no float squaring to
  Magnitude's floor). The first run left 72: the exact edges above and the rate's w after a base Read were missing.
- **Harness lessons (my mistakes):** the comparison first left out the rate's w on every track, though only the
  direction track's EndRead leaves it undefined (a mutant deleting the base Read's copy survived). A Python heredoc
  turned a `\n` into a newline in the C++ (the known Bash hazard), and a local named `far` hit windows.h's macro (the
  known one).

## Net part 3 (the transport's packets, pools, channels and messages, `net/net_channel.cpp`, 2026-11-24)

- **The build:** `net/build_chan.bat` (`net/chan_diff.cpp`, suite `net.chan`): net_channel.cpp and bitstream.cpp
  against the original's. Each program keeps its own pool (the port's g_netPool; the original's at 0x00983150, built by
  its constructor, its static initialiser never having run). Their packets and buffers are paired as they first appear
  (the free lists walked in order, the calls' results), so every later pointer is checked against its partner; a
  buffer over 1000 bytes (malloc'ed, freed: its address can come back) only for null or not. The channels are made by
  the original's constructors in buffers of its layout; their std::lists are MSVC6's, walked node by node. The
  original's operator new / delete, malloc / free and clock (0x00748dc0) are the test's; both programs' allocations
  are zeroed (a new packet's sequence number is never set, and Clear leaves it). The port's free is wrapped
  (`--wrap=free`) and both sides' deletes recorded for the destructors.
- **The tests:** rounds of up to 60 random calls on a plain, reliable or ordered channel (numbers near the 23-bit wrap
  now and then): packets from the pool (data of up to 1000 bytes, exactly 1000, bigger, none), Send, PopOutgoing,
  Receive (numbers near the newest or the last delivered, behind them, repeats of those held, across the wrap),
  PopReceived, WriteAck, ReadAck (near the last acknowledged, or anything), Resend (the clock moved on by up to 8 s; the
  resend time changed), Acknowledge, AddRef and Release (to the pool at the last), the packets' Write and Read (from
  where a packet was written), FreeData / Clear / FreePacket, the pool's buffers (past its 16: it grows; back in any
  order; none), UnwrapSequence at the edges of its thirds, the header's Write and Read, the packet list (packets added,
  written, read back from the pool, popped, the rest released by the destructor), the ping (a reply or not; a round
  trip of 0 read as 1), the destructors (a reference-counted object released below one, a packet with its data, a pool
  of its own grown and destroyed). After each call the channel, every packet the test holds, both pools' free lists
  and locks and the streams are compared. 30,000 rounds, about 60 million checks, 23 s (HD2_CHAN_QUICK: 8,000).
- **Coverage:** every line of net_channel.cpp and of net_channel.h's bodies.
- **Port bugs found:** none.
- **The original's weaknesses (kept):**
  - The plain and reliable channels' destructors forget the packets received (and the reliable one's held out of
    order) without releasing them: they never go back to the pool.
  - A packet's Read takes a pool buffer without giving back one it had; its sequence number is never cleared.
- **Mutations:** 240 mutants (`net/mutations/mutate_net3.py`), 20 not building; 210 of the 220 building killed. The 10
  left: the pool destructor's DeleteCriticalSection and its clearing of an object about to vanish (unobservable),
  three equivalent. The first run left 19: the locks were not checked.
- **Harness lessons (my mistakes):** the stream's write starts mixed acknowledgements with packets, so a packet "read"
  from an acknowledgement took a random size of up to 32 KB, and writing it back overran the test's buffer into its
  globals (the failure showed as a crash while printing the trace). The messages' room check asked for more than the
  buffer held, so they never ran until the coverage showed it. The delete recorder first grew a vector, whose own
  delete it recorded (a recursion).

## Net part 4a (the transport's UDP socket, `net/net_socket.cpp`, 2026-11-24)

- **The build:** `net/build_sock.bat` (`net/sock_diff.cpp`, suite `net.sock`): net_socket.cpp, net_channel.cpp and
  bitstream.cpp against the original's. No real socket or thread: every file is built with `-include sock_fakes.h`,
  which renames the port's Winsock and thread calls (WSAStartup, socket, bind, sendto, recvfrom, select,
  gethostname, gethostbyname, closesocket, WSACleanup, CreateThread, CreateEventA, SetEvent, ResetEvent,
  WaitForSingleObject, CloseHandle, SetThreadPriority) to the test's fakes; the original's import table slots holding
  those functions are pointed at the same fakes. The fakes log every call (compared) and answer from a script both
  sides read alike (select's answer, recvfrom's datagrams, the host); outside the compared calls they pass through to
  the real functions (the loader's). The original's socket is built by its constructor in a buffer of its layout (its
  fragment vector and lists MSVC6's); each program keeps its own pool (part 3); queued buffers are compared by bytes.
- **The tests:** rounds of up to 50 random calls: Seal / Unseal (sizes 0 to 600, other keys, a bit changed, another
  size), Send (whole under 500 bytes; fragments of 500 from 500, 501, 1000, up to 3000), FlushDelayed (the clock moved
  on or not; the last fragment's time at the clock's edges), SendQueued, SendThread (its waits answered until it is
  told to stop), the thread's entry as CreateThread was given it, QueueSend and SendRaw alone, Receive (nothing, an
  error, recvfrom's 0 or -1, a datagram the sockets sent: as sent, a bit changed, cut short; anything, sealed or not,
  a fragment or not; fragments in any order, repeated, expired after 20 s), TakeCounters, GetLocalAddress (the lookup
  failing at each step), Open (any port or -1; over an open socket) and Close. After each call the sockets (their
  buffers, fragments, delayed and queued datagrams, counters, thread fields, local address), the pools' free counts,
  g_netLastFragmentTime, the locks and the call logs are compared. 20,000 rounds, about 7 million checks, 20 s
  (HD2_SOCK_QUICK: 5,000).
- **Coverage:** every line of net_socket.cpp.
- **Port bug found and fixed:** LookupLocalAddress called WSACleanup after any lookup on a closed socket; the original
  only when a host was found (a failed lookup leaves its WSAStartup counted).
- **The original's weaknesses (kept):**
  - A datagram under 8 bytes whose fourth byte has the high bit (for one under 4 bytes: the buffer's old byte) passes
    Unseal as a fragment (under 7 bytes no word is checksummed) and its data's size comes out negative: the copy
    faults. Anyone who can send the game a UDP datagram can crash it, the port too. The test does not send such
    datagrams. A fix belongs on a branch.
  - The constructor leaves the byte counters, the thread's fields, the sockaddr and the local name past its first
    byte as the memory was (the test zeroes them; the port's start at 0): TakeCounters' first answer, and SendRaw
    before Open, use that memory.
  - Open starts the send thread before making the events it waits on (the port makes them first; the test compares
    Open's calls in any order).
  - Expired fragments and queued datagrams at destruction are not given back to the pool.
- **Mutations:** 277 mutants (`net/mutations/mutate_net4a.py`), 19 not building; 249 of the 258 building killed, the 9
  left equivalent or unobservable.
- **Harness lessons (my mistakes):** the fakes first numbered every handle from one counter, so the original's thread
  (made before its events) shifted the events' numbers; threads and events are counted apart now. A random datagram
  under 8 bytes crashed both programs before I understood the fragment test. Twice more a Python heredoc turned the
  C++ `\n` into newlines (the known Bash hazard: Edit for such text).

## Net part 4b (the transport's connections and their manager, `net/net_transport.cpp`, 2026-11-24)

- **The build:** `net/build_trans.bat` (`net/trans_diff.cpp`, suite `net.trans`): net_transport.cpp with the socket,
  channels and bit streams against the original's. Part 4a's fakes of the system calls, now shared
  (`net/sock_fakes.inc`, with a stop after some selects for the receive thread and a clock that moves on at each
  datagram received). Each transport talks to itself: what its socket sent (logged at sendto) comes back through
  recvfrom to the connection of the id in its header. The original's transport is built by its constructor in a
  buffer of its layout (its connection vector MSVC6's); its connections by theirs; its pool and send stream
  (0x009830e8), whose static constructors never ran, are built before the rounds; its malloc, realloc, free, new and
  delete are the test's.
- **The tests:** rounds of up to 60 random calls: Start (any port; over a started transport) and Stop, connections made
  (ids up to 255, repeated; the port known or not; the transport's session changed) and added, removed, deleted with
  DeleteConnections, found by id and by address, Lock / Unlock, Send (any channel, kind, up to 1500 bytes: fragments),
  Resend of a packet still to go, Receive (the data given back to the pool), SendPing, PingIfDue (at its 2 s),
  Flush and Gather (forced or not), SetResendTime (under 10, 10 to 700, over), ResendAll / AcknowledgeAll (the clock
  moved on), Tick (at its 500 ms), FlushAll, the socket's queued datagrams sent, ReceiveAll (up to 4 datagrams: as
  sent, a bit changed in a checksummed word, from another address, anything sealed or not; select's error now and
  then), the receive thread (its loop, or its entry as CreateThread was given it), HandleDatagram on a datagram's
  body (packets, a ping, an echo). After each call the transports (their connections in order, fields, locks),
  every connection (its fields, lock, its three channels' numbers and list lengths), the sockets' queues and numbers,
  the send stream's bytes, the pools' free counts and the call logs (sendto's bytes, the game's packet callback's
  calls) are compared. 40,000 rounds, about 15 million checks, 12 s (HD2_TRANS_QUICK: 10,000).
- **Coverage:** every line of net_transport.cpp.
- **Port bugs found:** none.
- **The original's weaknesses (kept):**
  - A packet's 16-bit size is read sign-extended: a datagram claiming a packet of 32 KB or more makes the receiver
    take a 1000-byte pool buffer and copy about 4 GB into it; sizes past the datagram's end read past it. Any sender
    of a packet list (type 1) to a connection's id crashes the game, the port too. Together with part 4a's short
    datagrams, the protocol needs bounds checks: a fix for a branch. The test sends no such datagram: random ones are
    never packet lists, and changed bits go only into checksummed words.
  - A datagram's last 1 to 3 bytes (past its whole words) are not checksummed, and the checksum is the sum's low 24
    bits, so a change in any word's top byte passes too: a quarter of single-bit changes go unseen (the test changes
    only bits the checksum sees; it first changed any, and the fix branch's guard then dropped what the original
    parsed as garbage).
  - Branch `net-robust-datagrams` (2026-11-24) drops the datagrams behind the remote faults of parts 4a and 4b and a
    message of fragments past the socket's 32 KB buffer (a heap overflow); its `net.robust` test sends them.
  - The constructor leaves the thread's fields, the times and the session as the memory was (Add before Start copies
    that session); the test zeroes them.
  - Start forgets the connections without deleting them.
- **Harness decisions:** Start's calls are compared in any order (part 4a: the socket's Open makes its events before
  the thread in the port). Resend only of a packet a channel still holds (the game never calls it).
- **Mutations:** 245 mutants (`net/mutations/mutate_net4b.py`), 19 not building; 219 of the 226 building
  killed. The first run left 9: Receive's null for none (the test had set it null itself), ReceiveAll's flush between
  datagrams (the clock never moved within a call) and the locks' Enter (now the lock word and owner are checked too)
  were not seen. The 7 left: the destructors' DeleteCriticalSection (54, 271 del); a Leave without its Enter in
  ResendAll and FindByAddress (238, 322 del: today's Windows ignores leaving a section the thread does not own, one
  thread cannot tell); Receive's clearing of a packet's size before releasing it (87 del, num: nothing reads it before
  Clear does the same); Remove's move of the last into its place when it is the only one (381 cmp: the same).
- **Harness lessons (my mistakes):** the original's realloc was not redirected, so the send stream's growth crashed
  in its uninitialised CRT heap; my direct HandleDatagram first forced type 1 on other bodies, and my changed copies
  first flipped the unchecked last bytes: both made garbage packet sizes that crash both programs. Again a Python
  heredoc turned a C++ `\n` into a newline.

## Net part 5 (the console's command processor, `net/net_console_commands.cpp`, 2026-11-24)

- **The build:** `net/build_cons.bat` (`net/cons_diff.cpp`, suite `net.cons`): net_console_commands.cpp (with
  game_state.cpp and c_str.cpp) against the original's ReadConsoleToken, Execute, ExecuteFile, the command processor's
  constructor and destructor and the console state's (C_ConsoleState) constructor, Start, ShowBanner and Update. What
  they reach is the test's on both sides, each faked in the program's own layout:
  - the profile's multiplayer settings (the original's a buffer of its layout pointed at by 0x008aea30: its maps
    vector MSVC6's, its map names' C_str at +1; the port's raw storage);
  - the set-up (its game type; GetCurrentMaps' groups for listmaps), the maps (AddMap takes a C_str in the original,
    RemoveMap a char *);
  - the game app (PrintMessage, PostCommand, SendGameMessage; its state's SetState and SetPendingState: a vtable of
    loggers on the original's side, an override on the port's), the console (AddLine, PopCommand), the texts ("T<id>",
    the original's made by its own C_fstr), the net game (0x0097d070: its players one array both read, LockPlayers
    answering the original's vector; FindPlayerByName, KickPlayer, KickPlayerByName, the vote's Start, Vote, CountYes,
    PostVote, IsPlayerAdmin, Host, Connect), the game (StartNextRound, ChangeMap, the mission's objectives at +0xA8 /
    +0x34), IGraph (SetFlags, GetHWND, InitMouse: one fake vtable) and ShowWindow (the port's renamed by
    `-include cons_fakes.h`, the original's import slot);
  - the original's malloc / free / new / delete and its fopen / fscanf / fclose (msvcrt's: "exec" reads real files).
  Every fake logs its call alike; the logs, the result, the settings (all their fields, the passwords' texts, the
  server entry's bytes), the set-up's game type and the net limits are compared after every command line.
- **The tests:** 30,000 rounds, each a random world (the role: local, server, client, dedicated, none; the settings,
  rules, maps, passwords; the players; the objectives; the answers of AddMap, RemoveMap, FindPlayerByName,
  IsPlayerAdmin, Vote; a game or none) and up to 6 command lines of random keywords (every one, any case, unknown
  words), values (signed numbers, quoted words, keywords, addresses), separators (spaces, tabs, '>', '-', '-->'), and
  "exec" of a file of such lines (CRLF or LF, a last line with or without its newline, a missing file); now and then
  the console state made (with a command or none) and started, its banner shown, updated (the console's typed
  commands, its own first command, the state asking to host or join, a dedicated server or not), or the command
  processor made and destroyed. About 480,000 checks, 17 s (HD2_CONS_QUICK: 6,000 rounds).
- **Coverage:** every line of net_console_commands.cpp.
- **Port bugs found and fixed:**
  - ReadConsoleToken skipped a '-' before a digit, so every negative value typed in the console lost its sign
    ("respawntime -7" set 7 where the original clamps -7 to 3). The original's test there is isalpha (0x007e69c3,
    named isalnum in the database: its mask 0x103 is the letters'); the header's comment said so too.
  - A rule given without a number showed `value` as it was (0, or a number read earlier in the line); the original
    loads the rule's own value into it first and shows that (the port's "Fix" comment had taken it for an
    uninitialised read). The old unit test (net.console) expected both mistakes and was changed with them.
- **The original's weaknesses (kept; the port's Fixes noted where it has them):**
  - mapname gives the word to C_fstr as its format ('%' in a map name; the port's Fix passes it as text).
  - adminlogin and svcmd copy at most 0x80 bytes without ending the copy: a longer text reads on past it (the port's
    Fix ends it; the test compares the first 127 characters).
  - "exec" reads lines with an unbounded %[^\n] (the port's Fix: 99 characters; the test's lines are shorter).
  - listobjectives reads the game's mission without checking there is a game: with none both programs fault (the test
    gives a game to every line, file or typed command that has it).
  - The console state's constructor copies its command unbounded (the port's Fix cuts it) and leaves its mode as the
    memory was (the test zeroes it).
- **Mutations:** 431 mutants (`net/mutations/mutate_net5.py`), 31 not building; 399 of the 400 building killed. The
  one left gives the keyword table a row more (equivalent).
- **Harness lessons (my mistakes):** I first took the original's rule display for an uninitialised read (the port's
  comment said so) and zeroed the stack under its call; the asm showed the rule's value. My SendGameMessage fake freed
  the chat line at once, but Execute prints it to the console after: both printed freed memory. My `listobjectives`
  guard first looked only at the command line, not at the "exec" files and the console state's commands. The
  database's name "isalnum" for 0x007e69c3 hid the port bug until the original's answer for '2' was printed. Python
  heredocs with C++ `\n` again (twice): Edit or a script file written by Write from now on.

## Net part 6 (the game's own console commands and their variables, `net/net_console_vars.cpp`, 2026-11-24)

- **The build:** `net/build_cvar.bat` (`net/cvar_diff.cpp`, suite `net.cvar`): net_console_vars.cpp against the
  original's ExecuteConsoleLine, ExecuteConsoleCommand, GetCheatValue and ConsoleValueInRange. What they reach is the
  test's on both sides, each in its program's layout: the game's Print, the controls' SetMouseScale, the net game's
  SendChat, the app's PostCommand, the profile's difficulty, the team manager's player team (GetActivePlayer,
  HealMembers), the active player (its virtual Hit: the port's slot found from the member pointer, the original's
  +0xD4; its net id), the items module (GiveItem; the infinite ammo flag, the original's byte 0x0098b02c), the item
  definitions (virtual GetBehaviour and GetAmmoItemId; the original's table 0x0098b030) and the command processor's
  Execute. The original's name lookup (a std::map at 0x0097b9cc) is made by its own static initialisers (0x007200c0,
  0x00720240), its std::_Lockit a no-op (one thread). After each call the result, the logs, the variables (GetCheatValue
  on both) and the infinite ammo flag are compared.
- **The tests:** 40,000 rounds, each a random world (the role, a local game more often; the difficulty; a player or
  none, its net id the local slot or not; the definitions of the given items' types, their ammo ids (none, 0, any) and
  kinds (thrown, a sticky charge, others); the answers) and up to 6 calls: lines (".text", "x,text", one letter, a table
  entry with its argument: numbers, hex, octal, quoted, a lone quote, words, every given item's name in any case, long
  texts; spaces before and after; unknown words and lines of 500 characters or so for the command processor), the
  commands alone, and ConsoleValueInRange on random ranges (its grammar: '!', '&', ',', n, n:m, <, <=, >, >=, %, '*';
  numbers of one, two and three digits, leading zeros, signs; or random characters) and numbers from -130 to 130.
  About 360,000 checks, 9 s (HD2_CVAR_QUICK: 10,000).
- **Coverage:** every line of net_console_vars.cpp but two the table cannot reach (a text variable's display; the
  switch's default: every entry is handled).
- **Port bug found and fixed:** the range language's numbers. The original's loop adds the first digit again for every
  digit after it (its character is loaded once, before the loop): "12" is 11, "123" 111, "01" 0. The port read them as
  written; it now does as the original (the table's ranges are "0,1": no visible effect in the game). The old unit
  test (net.console) expected the written values and was changed.
- **The original's weaknesses (kept):**
  - A comparison mark with no number after it ('<' at the end, "<=x") compares against an undefined value: the
    original's token reader sets only the kind at the end, the value staying what the caller's stack temporary held.
    The port takes 0; the test gives no such range.
  - '%' with no number, or 0, after it divides by zero (both programs; none given).
  - say / sayteam copy at most 100 characters without ending a longer copy (the port's Fix ends it; the test compares
    the first 100).
  - An empty line's second character is read (the game's chat buffer has zeros there; the test pads the lines).
- **Mutations:** 295 mutants (`net/mutations/mutate_net6.py`), 35 not building; 235 of the 260 building killed in the
  quick run, 236 with the full one (78 text, a rare item name, is killed by the full run). The first run left 44: every
  given item's name, an ammo id of 0 and numbers near the ranges' multi-digit values were missing. The 24 left:
  equivalent (16: the unused takesValue field and result codes, the '>=' mark's own number, value / sign for
  value * sign, initial values always replaced, sscanf's "%ix", the end token's value and cursor in the ranges the test
  gives), unreachable (2: the text variable's display), the chat copy's size and end (5: the original's characters past
  100 are undefined), an empty line's second character (1: zeros in the buffer).
- **Harness lessons (my mistakes):** I first read the original's end-of-range token as keeping the last token's value
  (it writes into a caller's stack temporary, undefined); the original's std::map construction crashed in std::_Lockit
  (a critical section its CRT start-up makes); the range test's numbers (-7 to 9) could not tell multi-digit parses
  apart until widened.

## Net part 7 (the network game's players, teams and scores, `net/net_players.cpp`, 2026-11-24)

- **The build:** `net/build_players.bat` (`net/players_diff.cpp`, `net/players_globals.cpp`, suite `net.players`):
  net_players.cpp against the original's 37 functions of the file (OnPlayerKilled, the kicks, the ranks, the scores
  and their message, the team counts, BalanceTeams, TeamsBalanced, IsTeamAlive, IsPlayerAdmin, CanPlayerRespawn,
  FillServerStatus, PrintTeams, FlipSessionParity, ResetReplication, GetConnectionState, SetLevelBounds, the
  quantisation table's constructor). The original's net game is its own global (0x0097d070), its players a block of
  0xC4-byte states with the same bytes as the port's (the humans and connections each side's own). What they reach is
  the test's on both sides, logging alike: the players' and queue's locks (the original's EnterCriticalSection /
  LeaveCriticalSection by its import slots, the port's C_NetLock), the game (Print, PrintColored, PrintMessage,
  SetRoundTime; its times), the app (SendGameMessage, PrintMessage), the texts, the actors by index, the humans
  (GetControllerType, GetTeam, the controller's IsSpectating and SendSoldierClass, the uniform's container, item and
  definition), DirectPlay's GetConnectionInfo (one COM object for both), the replication (its quantisation setters,
  its objects' ClearMessages), the pool's FreeBuffer and the perf clock. The original's quantisation table is made by
  its constructor (a static's, never run) and compared with the port's.
- **The tests:** 30,000 rounds, each a random world (the role, 6 to 12 players with random bytes and names (freed
  slots with leftovers after the first character, one letter, 51 letters), humans or none, teams 0 to 2, stats with
  the 11-bit score's edges, lives up to 200 and around 127; the rules; the game or none; 16 humans' answers; the
  actors; the team sums; the connections' sessions; the queues; the connection's age around 600 ms) and up to 8
  calls; the scores' message written by both (the bits compared) or read by both (a message the port writes in a
  server's role, changed a little, slots past the players). About 467,000 checks, 8 s (HD2_PLAYERS_QUICK: 8,000).
- **Coverage:** every line of net_players.cpp but the table's destructor (a static's, at exit) and its growth (the
  game sets entries 0 to 3 of 10).
- **Port bug found and fixed:** PrintTeams's texts: the original prints "Alied" (twice), the port "Allied". It is
  not called, and C_game::Print and PrintColored are empty in the release build: no effect in the game.
- **The original's weaknesses (kept):**
  - A non-player killed in co-op by the slot one past the players: the original's `<=` reads past them (the port's
    Fix stops it; not given).
  - The quantisation table's growth loses the new block (the copy's cursor is kept, the old block freed and still
    used): unreachable, the game sets entries 0 to 3 of 10.
  - The quantisations' w components and padding words come from the stack (the callers' arrays have three floats;
    the table's entries are copies of a stack temporary): not compared.
  - IsPlayerSpectating calls the controller of a player's or remote human unchecked (both programs; the game always
    gives them one: not given).
  - The scores' message writes and reads the co-op lives of slots 0 to 5 unchecked (both; the test has 6 slots or
    more).
- **Mutations:** 644 mutants (`net/mutations/mutate_net7.py`), 111 not building; 486 of the 533 building killed at
  first, 507 after the inputs above were widened (freed names with leftovers, the score's edges, lives of 127, the
  connection's age at 601 ms, the player table's globals compared). The 26 left: equivalent (21: array sizes and a
  buffer's, the 11-bit write's masking of a negative score, -1 against -2 for an unsigned slot check, a null actor's
  role, BalanceTeams's early return at 0 and 1), unreachable (4: the table's growth), past the players (1: a slot
  equal to their count reads past the vector).
- **Harness lessons (my mistakes):** building the read test's message with the port's WriteScores changed the port's
  pings (players' and teams', clamped) before the original's copy was made: they are now put back; the original's
  bit stream keeps its word index at +8 (I first read +0xC); I lost the first mutation run's survivor list by keeping
  only its last 15 lines (rerun with the whole output kept).

## Net part 8 (the replicated objects, `net/net_object.cpp`, 2026-11-24)

- **The build:** `net/build_repl.bat` (`net/repl_diff.cpp`, `net/repl_fakes.h`, suite `net.repl`): net_object.cpp
  against the original's properties, objects and replication. Each round makes a replication in each program (the
  original's by its constructor; its objects and properties by its own code), shares the test's quantisations (the
  direction tracks' are each program's globals), gives both the same factories by id (state readers and message
  factories, logging, in each program's convention: the original passes the id in ECX) and runs up to 40 calls:
  objects made, found, removed, deleted; properties added, their flags, fields, lag, value, rate, net value,
  direction; messages queued (the test's, in each program's layout: the original's vtable a copy of its own with the
  test's Write, Read and deleting destructor), popped, cleared; the send masks, the live and forwarding flags, the
  state handlers and factories; Update (the performance counter faked on both: the original's import slots, the
  port's through `-include repl_fakes.h`), Refresh, SetLag, the quantisations, the epochs, the view cone (the viewer's
  place and facing, objects around it and a hair off it); Write for a player (the bits compared) and Read of what it
  wrote, when every object written finds a reader of the same format (the objects' tracks' own flags, the
  quantisations, the factories and state handlers; the reader's side changed first at times: the epoch read, an
  object not live, one removed, the send masks), from a few bits in. After each call the logs, the results and the
  state are compared: every object's fields, its properties' bytes (the original's layout: the port's matches it), its
  queues (the messages by serial number and fields), the replication's fields and the local object.
- **The tests:** 20,000 rounds, about 1.7 million checks, 5 s (HD2_REPL_QUICK: 5,000).
- **Coverage:** every line of net_object.cpp.
- **Port bug found and fixed:** ClearMessages releasing a message another queue still holds: the original looks at
  it again (it does not move on), releasing it until it goes; the port released it once and kept it. The port now
  does as the original, keeping its Fix where the original would loop for ever (not releasing). Every caller in the
  game forces the clear: no effect in the game.
- **The original's weaknesses (kept):**
  - ClearMessages without releasing a message still held loops for ever (the port's Fix moves on; not given).
  - A server's forwarded message is in both its object's queues (received and to send): clearing one queue with
    force deletes it and leaves the other's pointer dangling. ResetReplication clears both with force: a message
    the server forwarded and had not yet sent is deleted twice, in both programs (not given; noted for a fix on a
    branch).
  - The constructors leave the properties' padding, every vector's w, the tracks' +0x1C and the direction track's
    turn sign as the memory was (not compared).
  - The view cone reads every object's property 0 unchecked, before asking whether the object has data to send (the
    test gives every object one first).
- **Mutations:** 406 mutants (`net/mutations/mutate_net8.py`, made again after the ClearMessages fix), 20 not
  building; 326 of the 386 building killed at first, 348 after the inputs were widened (the viewer's place and
  facing, read-backs from a few bits in, the reader's side changed, rates and net values, a 0.4 s silence exactly,
  the state handlers mostly in pairs). The 38 left: equivalent (12: the initial flags overwritten, the direction
  track's empty quantisation setters, GetCurrent and Refresh results dropped, the move-target flag already set at a
  0.4 s silence, q[4], the sign of a nonzero tiny component at >= or >), exact float boundaries not met (5: a square
  of 1 or 1e-8 exactly, a dot product equal to the cosine), ClearMessages where the original would loop for ever
  (3), memory only (7: deletions not observable here, Reset's clear only in the constructor, the global's definition
  this test does not read), the scratch object's property reads (10: the read-backs reach the scratch object only
  without properties written), past the vector (1: RemoveObject's `<=`).
- **Harness lessons (my mistakes):** I did not construct the original's message stream at first; a write read back
  went astray until the format check took the tracks' own flags (a new property's tracks keep theirs until a field
  is set), the 9-bit ids (-2 reads as 510) and the objects the read makes; the view cone reads every object's property
  0 before the data check (I checked only the objects with data); my end-of-round cleanup cleared both queues with
  force and deleted forwarded messages twice (each message is now freed once).

## Net part 9 (the network game's messages, `net/net_messages.cpp`, 2026-11-24)

- **The build:** `net/build_msgs.bat` (`net/msgs_diff.cpp`, `net/msgs_fakes.h`, `net/msgs_globals.cpp`, suite
  `net.msgs`): net_messages.cpp against the original's server message handler (HandleServerMessage), SendGameMessage,
  PostCommand, NetWriteSoldierState / NetReadSoldierState and the game messages' streams (BeginGameMessage,
  BeginCommand, BeginReply). The original's net game is its own global, its players the same bytes as the port's (the
  humans and connections each side's own), its five message streams made by its constructor over the test's buffers.
  What they reach is the test's on both sides, logging alike: the players' lock and the app's command lock (the
  original's critical sections by its import slots, the port's through `-include msgs_fakes.h`), timeGetTime,
  DirectPlay's GetClientInfo (one COM object), the name helpers (NetWideToUtf8 / Ansi, HasCjkCharacters,
  FindFreeNameNumber), the connections (made, deleted: by serial) and the transport (Add, Remove), the key checks'
  DropKeyQuery, GetHostAddress, the player counts and the status, QueueReceived, the game (OnPlayerLeft, the
  objectives), the app's lines, the texts, the map and the rules' writers, the modules' and temporary actors'
  states, the scores, SetPlayerTeam, FindPlayer, GetPlayerName, Process and SendNetMessage (each stream's fields and
  bits logged, then emptied). The players' places come from frames both programs read (the world matrix valid), the
  soldier states from GetSoldierState in each program's layout, the squad's first soldier made in both.
- **The tests:** 20,000 rounds, each a random world (the role, 1 to 12 players or 32 all connected; the humans' net
  ids, frames (facings too short to normalise), uniforms; the rules (dedicated, the listing, the password); the key
  checks (names and addresses matching the joiner's, any state)) and up to 8 calls: soldier states written and read
  back (names up to 300 characters, -1 fields, 260 belt items), the streams begun, game messages of every type queued
  and sent, commands posted and sent, DirectPlay's messages (a player created with any of GetClientInfo's answers,
  destroyed, a hosts query of any request, a connection asked for (versions, passwords, names, a full server), aborted,
  data, a buffer returned, the server's info, others). About 935,000 checks, 6 s (HD2_MSGS_QUICK: 5,000).
- **Coverage:** every line of net_messages.cpp but two Fixes the original cannot reach (it reads null): a place of a
  player without a human (game message 5) and an empty squad's state (commands 0 and 1).
- **Port bugs found and fixed:**
  - The facings' normalisation: the players' places (game message 5) sum x, y, z (the port had the whole game's
    order) and the whole game's (0x15) y and z, then x (the port had the places' order). The last bits of the
    directions sent differed.
  - A joining player's CD key: the original refuses the key (5) at the first check of that name and address that is
    neither good nor in use; the port looked on for a later good one.
- **The original's weaknesses (kept):**
  - An aborted connection's link is deleted and left in its slot: a second abort or a destroy of that slot before the
    slot is reused deletes it again (both programs; the game reuses the slot first: not given).
  - A joining player's slot (and the server's own) is filled from a state constructed on the stack: the name's bytes
    after the first, +0x78, +0x80..+0xA0, +0xA3, the activity time and +0xC0 (the server's own slot also the address)
    are the stack's leftovers (the port zeroes them; not compared in that call, then carried over).
  - The hosts query's reply is written into a block operator new gives uncleared, and WriteBits ORs its bits in: the
    block's leftovers can set bits of the reply (both programs; the test gives both zeroed blocks).
  - A place of a player without a human and an empty squad's state read null (the port's Fixes).
- **Mutations:** 717 mutants (`net/mutations/mutate_net9.py`), 125 not building; 559 of the 592 building killed at
  first, 566 after the test logged DirectPlay's info block, GetSoldierState's -1 fields and the reply handed over and
  gave the streams any channel. The 26 left: equivalent (12: buffer and block sizes, an unused member and `(void)`,
  the sign of a nonzero tiny component at >= or >, the slot already at the end, the fresh state's key check already
  -1, GetSender(-2) as -1), the test's zeroed allocations (1: the info block's memset), exact float boundaries (4: a
  square of 1 or 1e-8 exactly), the Fixes the original cannot reach (6), memory only (2: the spawn's state and block
  not freed), past the vector (1: the free slot's `<=`).
- **Harness lessons (my mistakes):** I took NetWideToUtf8 / Ansi and GetHostAddress for register calls (they take
  their arguments on the stack and pop them); the spawn's facing is read with the player table's second entry (the
  recipient's), which my table did not have; my copy of the player state's constructor defaulted every field (the
  port's sets the teams to 2 and the key check to -1, as the original); an aborted slot's deleted link can match a new
  link's address on one side only (such slots are tracked until reused); the port's allocations are zeroed like the
  original's fake ones (a reply's bits depend on the block's leftovers).

## Net part 10 (the game commands, `net/net_commands.cpp`, 2026-11-24)

- **The build:** `net/build_cmds.bat` (`net/cmds_diff.cpp`, `net/cmds_fakes.h`, `net/cmds_globals.cpp`, suite
  `net.cmds`): net_commands.cpp against the original's HandleServerCommand (0..12) and HandleClientCommand (0..26, the
  whole game too). The commands' bits are the test's (each command's fields, then random bits), read by both from the
  same words. What they reach is the test's on both sides, logging alike: the players' lock, timeGetTime, the game
  (joined, respawned, left, the objectives, the rounds, the warmup, the camera, the in-game menu, the messages), the
  app (its messages and commands sent, its lines), the app's states (the test's objects with each program's vtable
  layout; the game and the main menu the joined session makes: the port's own classes with the test's stubs, the
  original's constructor, Start, Stop and deleting destructor faked alike), the loading screen, the campaign, the
  menus, the soldier profiles and stats, the uniforms' definitions (the original's inline table), the humans (their
  hits, respawns, chat text, the remote hits, their controllers), the spawn points, the team manager, the net game's
  player calls, the votes, the modules' and other files' readers (reading a few bits each), the command processor (the
  original's global set: it makes one when null). The port's NetReadSoldierState is net_messages.cpp's (compared by net
  part 9), copied into the test.
- **The tests:** 20,000 rounds, each a random world (the role, 1 to 12 players with freed slots' leftovers, the app's
  state of any type or none, the game or none, humans alive, dead or at 1 health, the squad's soldier or none, the
  profiles, the rules) and up to 6 commands. About 225,000 checks, 2 s (HD2_CMDS_QUICK: 5,000).
- **Coverage:** every line of net_commands.cpp (the dedicated server's console block is compiled out here).
- **Port bugs found and fixed:**
  - A chat line's sender's activity time: the original locks the players before reading the clock; C++17 evaluates
    an assignment's right side first, so the port read the clock first.
  - A player's team (client command 14): the original asks the player's team first, the local player's too; the port
    asked only for the others.
- **The original's weaknesses (kept):**
  - A VIP's uniform definition is read unchecked (the port's Fix counts none as no weight; the test gives VIPs a
    known uniform).
  - An empty squad's soldier is read through null (client commands 12 and 16; the port's Fixes).
  - The whole game's soldiers' controllers are called unchecked (a soldier made always has one).
  - The respawn's facing's w is the stack's (the port's 0; not compared).
  - The slots the session joined and the whole game append come from states constructed on the stack (their
    leftovers not compared in that call, then carried over).
- **Mutations:** 553 mutants (`net/mutations/mutate_net10.py`), 121 not building; 401 of the 432 building killed at
  first, 403 after freed slots' names and a health of 1 were added. The 29 left: compiled out (13: the dedicated
  server's console block), equivalent (15: the state's defaults the read overwrites, the facing's w, GetSender and
  ReadDirection3D with -2 as -1, a buffer's size, the stats all read, the whole game's unused place and facing), a Fix
  the original cannot reach (1: an unknown VIP uniform's weight).
- **Harness lessons (my mistakes):** ShowGameMenuMessage and PrintVoteResult pop their argument (my cdecl fakes left
  the stack 4 bytes off: a later local read garbage); the original makes the command processor itself when its global
  is null; the session joined's main menu ran the original's real constructor, Start and Stop until faked; the game
  objects had no vtable for GetState; the original builds the scratch rules inline (no constructor call to log) and
  reads GetItemDef inline; my first fresh-slot handling missed the appended slots.

## Net part 11a (the network game's core, `net/net_game.cpp`, 2026-11-24)

- **The build:** `net/build_ng.bat` (`net/ng_diff.cpp`, `net/ng_diff_calls.inc`, `net/ng_fakes.h`, `net/ng_globals.cpp`,
  suite `net.ng`): net_game.cpp against the original's net game, its own global made by its constructor and Init
  (statics never run). Each round sets both alike (the role, the local slot, 1 to 10 players with the same bytes and
  each side's humans and connections, the send period and clocks, the vote, the rules, the queues, the streams,
  DirectPlay's objects, the voices, the replication, the limits, the browser, the transport's callback, the session's
  text) and runs up to 10 of 36 calls: the vote (Start, Begin, Vote, CountYes, End, UpdateVote, PrintVoteResult,
  PostVote), the streams and queues (BeginMessage, CreateMessageEx, SendToPlayers, QueueReceived, QueueOutgoing,
  DispatchNetMessage, ReceiveMessages), the frame's sending (Process, SendJoinData, SendMessages), the commands (the
  kicks, the level scan, BitsForRange), the players (ResetScores, ResetPlayerStats, SetPlayer, SetVoiceValue, SendChat,
  GetBestScore, LockPlayers), Init, Close, Shutdown, the constructor. What they reach is the test's on both sides,
  logging alike (the locks, the clocks, the transport, the pool, DirectPlay, the voice, the replication, the app, the
  game, the modules, the console, the command processor, the texts, the scene's frames, the firewall, IGraph, GameSpy,
  the key socket, the query thread).
- **The tests:** 20,000 rounds, about 397,000 checks, 3 s (HD2_NG_QUICK: 5,000 rounds).
- **Coverage:** every line of net_game.cpp but the destructor's (it runs at exit).
- **Port bugs found and fixed:**
  - The level scan's bits (Command 3): the original gives BitsForRange the level's size in metres and counts it in
    centimetres (x 100); the port passed 0 and did not scale (every axis 1 bit, no steps).
  - A vote begun: the original prints "%s %d - %s" (the command, its argument, the text 0x22E2); the port printed the
    text, the argument and the command.
- **The original's weaknesses (kept):**
  - The kicks (Command 0 and 1) call DirectPlay's server unchecked (only the dedicated server's console asks: it
    has one; the test gives them one).
  - The lags (Process) read the replication's local object unchecked: a client with a server link and no
    replication reads through null (logged as such on both sides).
  - The level's corner and size are 4-float vectors whose w is the original's stack's (not compared).
  - Host writes the local player's name into slot 0 after Close emptied the players (part 11b).
- **Mutations:** 804 mutants (`net/mutations/mutate_net11a.py`), 101 not building; 639 of the 703 building killed at
  first, 659 after the test was widened (the constructor; the 5 s, 300 s and 15 s boundaries; messages filling their
  words exactly; the replication's stream capacity; the enum handle, reporting, +505C, the browser, the transport's
  callback and the session's text set and compared). The 44 left: compiled out (6: the dedicated server's file and
  watchdog), never run (1: the destructor), equivalent (37: a static_assert; values the members' initialisers or the
  player state's constructor already set; buffers' sizes; a role, a join stage and a total overwritten before being
  read; channel 2 either way; an unused argument; the level's w; equal bounds and scores either way; the frames'
  callback's result, which the test's sector ignores).
- **Harness lessons (my mistakes):** UpdateVote's `this` is the vote (+0x4D0), DispatchNetMessage pops its two
  arguments, qr2_shutdown takes its socket in ECX, BitsForRange takes the steps in ECX and the range on the stack (not
  ST0); the console's queue was shared, so the port popped the original's commands; the mission's flag is the game's
  +0x14 (the test wrote +0x24); comparing the level's w (the original's is its stack's).

## Net part 11b (the DirectPlay session, `net/net_session.cpp`, 2026-11-25)

- **The build:** `net/build_sess.bat` (`net/sess_diff.cpp`, `net/sess_diff_calls.inc`, `net/sess_fakes.h`,
  `net/sess_globals.cpp`, suite `net.sess`): net_session.cpp (with net_game.cpp: Close, QueueOutgoing) against the
  original's, its net game its own global made by its constructor and Init. Each round sets both alike (the role, the
  local slot, 1 to 10 players, the rules, the profile's name and font, the versions, DirectPlay's objects, the voices,
  the replication, the server entry, the status, the string +501C) and runs up to 8 of 34 calls: the text
  conversions (ANSI and UTF-8 both ways), GetHostAddress, Host, HostDedicated, CreateClient, CreateAddresses,
  EnumHosts, PingServer, PingServers, QueryServerStatus, Connect, OnConnected, the client's and server's message
  handlers (a connection's reply, a host's answer in either layout with a status or not, a datagram, a buffer given
  back, the session's end, others), the voice handlers, MakeReplication and its factories (by the object's id: an
  actor of each type), DeleteReplication, the voices started and stopped, MakeVoiceBuffer, VisitPlayers,
  UpdateVoiceTeam, TakeBandwidth, a status, its rules and a player written. COM is the test's on both sides
  (CoCreateInstance: DirectPlay's server, client and addresses, their calls' arguments logged and results scripted,
  failures with and without their object), and so are the voice, the replication, the transport, the key query,
  GameSpy's reporting, the firewall, IGraph, ShowWindow, the dedicated server's process, the app, the game, the
  humans' teams, the actors, the stats' bits, HasCjkCharacters, the server entries' reset.
- **The tests:** 20,000 rounds, about 312,000 checks, 10 s (HD2_SESS_QUICK: 5,000 rounds).
- **Coverage:** every line of net_session.cpp (the dedicated server's blocks are compiled out here).
- **Port bugs found:** none.
- **The original's weaknesses (kept; the test keeps away from them):**
  - Host: the session's wide name has room for 52 characters on the stack before the "Creating" line, which
    overwrites the rest before Host is called (the port's has 200).
  - Host writes the local player's name into slot 0 after Close emptied the players (the vector's spare room).
  - Host, no device: the device's GUID is read past an empty buffer.
  - Connect: CreateClient's failure is not checked (the client's object used); its wide copy of the player's name has
    room for 45 characters (a longer one overruns its stack).
  - CreateAddresses: the addresses' creation is not checked.
  - The client's handler: an accepted connection's reply uses the server's link unchecked; a host's answer calls the
    search and status callbacks unchecked and reads a status's player count unbounded (misaligned data reads on).
  - HostDedicated: the app's state is used unchecked.
- **Mutations:** 853 mutants (`net/mutations/mutate_net11b.py`), 109 not building; 684 of the 744 building killed
  at first, 693 after the test was widened (the factories' id boundary, a key check that leaves the join status, every
  byte of a found server's entry, random bits after a host's answer). The 51 left: compiled out (17: the dedicated
  server's blocks), unreachable (3: DirectPlay is never missing), equivalent (31: the conversion's flag 1 ignored,
  buffers' sizes, the address's type the fake writes, a format's trailing letter, values already set or written over,
  locals cleared after their release, the join request's name cut only past 45 characters (the original's stack
  overruns first), the reader not checking its capacity, VisitPlayers's empty loop, 17 team players (the original
  writes past its 16)).
- **Harness lessons (my mistakes):** -include renames reach original_exe.cpp too (its relaunch's CreateProcessA:
  the fakes pass through when not faking); GetComponentByName's type is the original's stack's (not logged); the
  replication's quantisation vectors' w is the original's stack's; failures a fake returns must match what the code
  checks (a failed address creation, an unset callback, a missing state crash both programs); the status data must
  stay aligned with the reader (a DEMO name or a newer server's layout changes what follows); the test's first
  survivors' check by hand looked at every mutation kind of a line, not the one that survived.

## Net part 12 (the voice wrapper, the firewall, the LAN search and CD-key checks, 2026-11-25)

### 12a: `net/net_voice.cpp`

- **The build:** `net/build_voice.bat` (`net/voice_diff.cpp`, suite `net.voice`): C_NetVoice against the original's
  (constructor, Init, StartServer, CheckAudio, StartClient, SetClientConfig, Stop, StopClient, StopServer, the
  destructor), DirectPlay Voice's test, server and client objects the test's on both sides (their calls logged, their
  results scripted, failures with and without their object), fresh blocks filled with garbage alike.
- **The tests:** 50,000 rounds, about 505,000 checks (HD2_VOICE_QUICK: 10,000). **Coverage:** every line.
- **Port bug found and fixed:** StartClient's last argument (the port's "unused") is the sound configuration's
  playback DirectSound object in the original; the port left it null (the game passes 0).
- **The original's weaknesses (kept):** a sound device configuration's size under 8 is written past; its block is not
  freed when the second GetSoundDeviceConfig fails.
- **Mutations:** 112 (`net/mutations/mutate_net12a.py`), 28 not building; 73 of the 84 building killed at first, 74
  after fresh blocks held garbage. The 10 left are equivalent (a static_assert; 0 either way; the member's
  initialiser; the next statement returns 0 too).

### 12b: `net/net_firewall.cpp`, `net/net_create.cpp`

- **The build:** `net/build_fw.bat` (`net/fw_diff.cpp`, `net/fw_fakes.h`, suite `net.fw`; the port's file included
  by the test, MUT_WRAPPER=fw_diff.cpp): ConfigureFirewall, FirewallBlocks, MapConnections, RemovePortMapping,
  AddPortMapping, the dialogs' watcher, NetFirewallAvailable against the original's. The network connections, their
  properties, sharing configurations and port mappings are the test's COM objects on both sides (every call logged by
  object, every result scripted), and so are the version, COM's start, the watcher's thread, the windows, Sleep, the
  strings and variants.
- **The tests:** 50,000 rounds, about 534,000 checks. **Coverage:** every line and branch of both files.
- **Port bug found and fixed:** MapConnections returns get__NewEnum's result when the collection hands no items (the
  port kept get_EnumEveryConnection's).
- **Mutations:** 221 + 10 (`mutate_net12b.py`, `mutate_net12b_create.py`), 67 not building; 161 of the 164 building
  killed at first, 163 after the watcher's flag was logged at the thread's start. 1 left: MultiByteToWideChar's flag
  1 (MB_PRECOMPOSED) ignored.

### 12c: `net/net_search.cpp`

- **The build:** `net/build_search.bat` (`net/search_diff.cpp`, `net/search_fakes.h`, `net/search_globals.cpp`,
  suite `net.search`): every function of the file against the original's (the browser's callback and its server
  entry, the searches, the reporting's key, player, team, list and count callbacks, the reporting started and
  answered, the key socket opened, read and written, RandomName, OnKeyChecked, FindFreeNameNumber, the server's key
  checks, the client's key query, DropKeyQuery). The port's net game is raw storage (net_game.cpp not linked); GameSpy
  (qr2, gcd, the browser: the original's __fastcall), the key socket, Sleep, rand, the perf clock, the locks, the net
  game's other calls, the main menu, the CD key are the test's on both sides; the key packets are made as the other
  side sends them (XORed, checksummed, a wrong checksum sometimes).
- **The tests:** 30,000 rounds, about 404,000 checks. **Coverage:** every line.
- **Port bugs found:** none.
- **The original's weaknesses (kept; the test keeps away from them):** a found server's name, map and game type are
  copied unbounded (a longer one overruns the entry and the stack); a key packet's name is copied unbounded and, with
  a number after it, overruns a check's name; a packet without its terminator is read on into the stack; a response
  without its ':' compares the stack's leftovers as the name; ReportPlayerKey reads 32 slots whatever the table's
  size (the port's Fix stops at the table).
- **Mutations:** 475 (`mutate_net12c.py`), 43 not building; 399 of the 432 building killed at first, 404 after the
  test was widened (a player past the 32 slots read, 49 to 51-letter names, one-letter responses, a response for a
  challenged check, query 0 often). The 28 left: buffers' sizes (7), values already set or written over (5), cuts
  the original cannot reach without overrunning first (9), equivalent (7: an empty or failed datagram fails the
  checksum either way, the XOR's extra byte is overwritten by the checksum, 32 either way, the 101st "check" never
  matches, a packet's size is never 0 or 1).
- **Harness lessons (my mistakes):** my logging operator new built its text by allocating (unbounded recursion), then
  let the log's own growth log; the map's name is at +1 in the original but +4 in the port's own class (each side
  its object); the original's browser functions take their server on the stack; heredocs collapsed backslashes in my
  edit scripts again (the memory's rule).

## Net part 13a (the GameSpy SDK's utilities, `net/gamespy_util.cpp`, 2026-11-25)

- **The build:** `net/build_gsutil.bat` (`net/gsutil_diff.cpp`, suite `net.gsutil`; the port's file included by the
  test, MUT_WRAPPER=gsutil_diff.cpp): the dynamic array, the hash table, MD5, goastrdup, the socket helpers, the
  buckets, the persistent storage's replies, the stats' buckets and the random numbers against the original's
  (__fastcall; its compare callbacks __cdecl, its free, hash, map, op and persist callbacks __fastcall: each side its
  own, logging alike). Each round makes the same structures on both sides (an array of 4 to 24-byte elements, a hash
  table, a bucket set of ints, doubles and strings as the default, the persist requests, a stats game) and runs up to
  12 calls; the allocations (fresh blocks filled with garbage alike), the CRT's qsort, select and WSAStartup are the
  test's on both sides. (GetUniqueId and GenerateUniqueId are not run: nothing calls them, and they write the registry.)
- **The tests:** 40,000 rounds, about 817,000 checks. **Coverage:** every line but those two functions and MD5's
  carry into its high count (an input of 512 MB).
- **Port bugs found and fixed:**
  - The persist callbacks get the request's local id and key first (the original's; the port dropped them).
  - BucketSet, BucketAdd, BucketSub, BucketMult, BucketDiv and BucketAvg hand back the bucket's data (null: no bucket
    of that name; a string's text when the op cannot take one), as the original's do and the stats' buckets use; the
    port's returned nothing.
  - The doubles' operations round as the original's x87 does under the game's FPU mode (24-bit precision).
- **The original's weaknesses (kept):** BucketSetToString's text grows once, to twice its size, whatever a bucket
  needs (a longer one overruns the block; the test's strings stay within).
- **Mutations:** 646 (`net/mutations/mutate_net13a.py`), 108 not building; 448 of the 538 building killed at first,
  460 after the test was widened (garbage in fresh blocks, the new table's callbacks compared, long strings of every
  length to 240, seeds meeting 0x7FFFFFFF). The 78 left: not run (37: GetUniqueId, GenerateUniqueId), unreachable (7:
  MD5's high count), not reached (3: the text's size within one of what a bucket needs), equivalent (31: buffers' and
  locals' sizes, a byte past the local digest, the local MD5 context cleared, empty moves at the end, the size given
  to an unbounded print, the original's stack's high half of a new int bucket, ValueForKey's two results never used
  together, a request index of -2 handled as -1, a set's size not passed on, the random number's initialiser).
- **Harness lessons (my mistakes):** BucketConcat's real entry is 0x00733700 (0x00733710 is its continuation); the
  original's CRT needs its float conversions set up (0x007e599c) before %f; FreeBucketSet frees the set itself (the
  test's were static); the test's log of a freed element read past an 8-byte table element; each side's bucket arrays
  got their own random capacities until one was drawn for both.

## Net part 13b (GameSpy's CD key client and server, `net/gamespy_gcd.cpp`, 2026-11-25)

- **The build:** `net/build_gcd.bat` (`net/gcd_diff.cpp`, suite `net.gcd`; the port's file included by the test,
  MUT_WRAPPER=gcd_diff.cpp): the response to a challenge, the products and their clients, the validation server's
  packets (uok, unok, ison, ucount and junk, XORed), the checks' think, the disconnects and the installer key's check
  against the original's (__fastcall; each side its own authentication callback, logging alike). Each round makes the
  same products and clients on both sides (ids, keys, session keys, addresses, times, tries, states, errors,
  requests), the socket, the server's address, the clock (2 s after a request, or just before, sometimes), the random
  numbers and the packets the socket will receive, and runs up to 8 calls; the results, the callbacks', sendto's,
  the clock's and rand's logs and the products are compared. The original's XOR key (a static initialiser the
  harness's copy never ran) is set up by the test.
- **The tests:** 40,000 rounds, about 531,000 checks. **Coverage:** every line.
- **Port bugs found:** none (the port's guard against a packet without a second backslash was already there).
- **The original's weaknesses (kept on main):** a packet of the validation server's that starts with a backslash and
  has no second one makes the original copy from a null pointer's distance (a crash; the port returns, marked Fix);
  the test sends none.
- **Mutations:** 321 (`net/mutations/mutate_net13b.py`), 70 not building; 218 of the 251 building killed at first, 223
  after the test was widened (the retry time's edge, error messages of 198 to 202 letters, a response's input of 511
  or 512 letters, keys with letters that are no hex digits). The 28 left: leaks (8: a removed client's frees),
  equivalent (20: buffers' sizes and slack, formats' trailing letters, ValueForKey's two buffers, commands that match
  none, zero statics reset, the scan's "0x" prefix).
- **Harness lessons (my mistakes):** the mutation runner defines GCD_TEST_PORT, which moved the validation port and
  failed the first run's baseline (the test now undefines it); the original's XOR key is a static the harness's copy
  never initialised.

## Net part 13c (GameSpy's query & reporting 2, `net/gamespy_qr2.cpp`, 2026-11-25)

- **The build:** `net/build_qr2.bat` (`net/qr2_diff.cpp`, suite `net.qr2`; the port's file and gamespy_util.cpp
  included by the test, MUT_WRAPPER=qr2_diff.cpp): the record made by qr2_init (or the default one), its socket (the
  first free port), the heartbeats and keepalives, the queries' replies (by keys, and the old `\status\` form), the
  master's challenge (RC4 and base 64), the echo, the add error, the client messages, the think, the shutdown, the
  buffers and key buffers, the key names' registration, the machine's addresses against the original's (__fastcall;
  the game's callbacks, its server's, players' and teams' keys, key lists, counts, add error and CD key packets, each
  side its own, logging alike). Each round makes the same record on both sides, the machine's addresses, the clock
  (a heartbeat's, retry's or keepalive's edge sometimes), the random numbers and the packets the socket will receive,
  and runs up to 8 calls; the results, the logs (sendto's, the callbacks', the sockets', the clock's) and both
  records are compared. The original's key name table is its data; the game's three registered names are set on both.
- **The tests:** 30,000 rounds, about 463,000 checks. **Coverage:** every line.
- **Port bugs found:** none.
- **The original's weaknesses (kept on main):** its key name table has 254 entries: a key of 254 or 255 reads (and
  qr2_register_key 254 writes) the "score_t" text after it (the test uses neither).
- **Mutations:** 491 (`net/mutations/mutate_net13c.py`), 39 not building; 420 of the 452 building killed at first, 432
  after the test was widened (the times' edges, the broadcast address as the host, buffers near full, the cipher's
  byte after its data, key 255, the default record's socket). The 20 left: frees (2), not reached (6: one byte left
  before a key list's end, an empty challenge after a zero byte, the exiting heartbeat's full buffer), equivalent (12).
- **Harness lessons (my mistakes):** the original's key name table is read at its address (0x00845828), not through
  a pointer stored there; a getsockname failure was not scripted until coverage showed its lines missed.

## Net part 13d (GameSpy's server browser, `net/gamespy_browser.cpp`, 2026-11-25)

- **The build:** `net/build_browser.bat` (`net/browser_diff.cpp`, suite `net.browser`; the port's file and
  gamespy_util.cpp included by the test, MUT_WRAPPER=browser_diff.cpp): the shared strings, a server's values and the
  queries' answers (binary and the old text), the query engine, the master server's cipher, the list (its request,
  the main list, the messages after it, the LAN answers), the browser's events and the API against the original's
  (__fastcall; the browser's callback each side's own, logging alike, reading the server's values as the game does).
  Each round makes a browser on each side and runs up to 10 calls: updates (asynchronous or thinking to their end),
  requests with options, thinks, clears, the values of the list's servers, the cipher, the parsing helpers, detached
  servers, the list's helpers, ListSendData, the trigger wait, the message readers, the engine by itself, the LAN
  answers, the shared strings. A scripted master server answers each request sent with a stream made for the reading
  side's own challenge (the cipher's header, the key list, popular values, servers, the end, messages), cut into
  reads; the servers being asked answer by their public or private addresses. After each call the result, the logs
  (the sockets', the callback's, the clock's, rand's, Sleep's) and both browsers (engine, lists, servers and their
  values, key list, input, cipher) and shared string tables are compared. The original's shared strings table (a TLS
  slot the harness's copy never set up) and its realloc, msleep and clock are redirected to the test's.
- **The tests:** 25,000 rounds, about 697,000 checks (seeds 1 to 3). **Coverage:** every line but two out-of-memory
  lines and one unreachable (ParseMainList with nothing read). Not tested: the dedicated server's sort and count
  (not in the game's executable), the dead LAN update (not ported).
- **Port bugs found and fixed:**
  - S_ServerBrowser was 4 bytes short: the original keeps the trigger's port at +0x614 (the callback at +0x618).
  - The list's events and the queries' events end a trigger wait when its server is heard from (ClearTrigger); the
    port had left that out, so ServerBrowserWaitForTrigger waited for ever.
  - ReceiveReplies gives an answer to the first server at its address only (the original's break); the port gave it
    to every one.
  - ListInit sets the sort key to "" (the port left it unset).
- **The original's weaknesses (kept on main):**
  - HasFullKeys is asked from a record's fixed fields: a record with both kinds of keys cut by a read can pass it and
    be read past what arrived (later fields then come from stale bytes).
  - A popular value's index is not checked against the list's count; fixed fields before any key list read a missing
    list (a crash).
  - A server message with a nonstandard port of 5 or 6 bytes leaves its port as the stack's (the port: 0).
  - ProcessMessages stays on a message under 3 bytes for ever (the port's Fix: an error): an empty error text's
    terminator left unread is such a start.
  - An empty old-style answer is parsed from its second byte (the stack's bytes past its end).
  - A new request keeps the previous connection's unread bytes, read as the new stream's start.
  - ServerBrowserNew leaves the trigger address and disconnectOnComplete as the heap gave them (the port clears them).
  The test's streams stay within these.
- **Mutations:** 1147 (`net/mutations/mutate_net13d.py`, the dedicated server's block left out), 92 not building; 964
  of the 1055 building killed at first, 989 after the test was widened (send results scripted, the LAN time-out's
  edge, bytes after the answer's tables, a value past the keys, texts of the limit's length, an empty error text,
  empty key names, a trigger on a server being asked, a field list reaching the text's limit exactly, unqueued
  servers queued). The 66 left: leaks (5), not reached (8: out of memory, rare cuts and sends), equivalent (53).
- **Harness lessons (my mistakes):** the original's FreeServer takes the server's address and its shared string
  functions an unused list first; its realloc must be the test's like malloc and free; the test's own streams broke
  the protocol at first (messages after a list with no end, a stream for a failed request, a new stream mid-parse,
  key lists replaced mid-parse, strings released that the test never added); the error message's pointer was
  compared after its buffer was freed; a field-list generator looped for ever; backslashes in Bash heredocs again.

## Net part 14 (the network's odds and ends: `net/net_console.cpp`, `net/net_com.cpp`, 2026-11-25)

- **The build:** `net/build_misc.bat` (`net/misc_diff.cpp`, suite `net.misc`; net_console.cpp included by the test,
  its loading flag and key buffer being in an unnamed namespace). PrintLoadProgress (0x00731af0) and GetCDKey
  (0x00732000) against the original's: each round sets both sides alike (the loading flag, the key buffer's bytes,
  the current map's name: the original's map packed, its name's C_str at +1) and calls one of them on both. The
  console (OnChar, MoveInputLineToHistory, AddLine; the original's getter given a made console), the texts, the
  current map and the registry are the test's, logging; the registry is scripted (the open's and the query's results,
  the key's handle, -1 and 0 among them, the value's type and length: empty, to 0x42 bytes, too long), never the real
  one (the original's by its import slots 0x0080f000 / 04 / 18, the port's renamed). After each call the logs (every
  argument), the result, the loading flag, the key buffer and the original's heap balance are compared. The GUIDs of
  net_com.cpp are compared with the original's data at their addresses (each one read by the original's code), and
  the voice codecs' order with StartVoiceServer's switch (0x00724520: the default's read, the jump table 0x00724704,
  each case's GUID).
- **The tests:** 40,000 rounds, about 172,000 checks, 0.2 s (seeds 1 to 3; HD2_MISC_QUICK: 10,000).
  **Coverage:** every line of net_console.cpp (net_com.cpp is data).
- **Port bugs found:** none.
- **The original's weaknesses (kept):**
  - GetCDKey queries the key -1 when the open fails (an invalid handle: the query fails).
  - A key of exactly 0x40 bytes without a terminator is returned unterminated (the buffer has no room for one).
- **Mutations:** 68 (`net/mutations/mutate_net14.py`), 12 not building; all 56 building killed (net_console.cpp 30;
  net_com.cpp 26: each GUID's last byte changed, two codec rows swapped).
- **Harness lessons (my mistakes):** the mutation runner in wrapper mode rebuilds only the wrapper, so net_com.cpp's
  mutants all survived the first run (it was never rebuilt): they run without MUT_WRAPPER; the labels passed from a
  Python print through Bash kept a carriage return and matched nothing.
- **Not tested (net):** `net_autorestart.cpp` (the dedicated server's only).

## Mission loader part 1 (the mission's loading, `mission_loader.cpp`, 2026-11-25)

- **The build:** `script/build_ml.bat` (`script/ml_diff.cpp`, suite `mission.loader`), new. It includes `cmd_diff.cpp` for
  its world, fakes and call logger; the loader's own calls go through `ml_calls.s` / `ml_calls.inc` (`gen_cmd_calls.py
  ml`, the generator's table grown by the loader's calls; IImage's and ISND_driver's slots now read too).
  `resource_cache.cpp` is linked: the model, sound, shadow and version model caches run on both sides (the original
  inlines their OpenInto and Clear in the loader), the original's built by its static initialisers' constructors.
  The original's loader is built by its maps' constructors (0x006cb5d0, 0x006cb430); its frame map (an MSVC 6 tree)
  walked for the comparison. LoadMission's copy protection (its two blocks) is jumped over in the original's copy.
- **The cases:** each case picks a mission: one of the game's (H:\HD2_Unpacked\Missions: all 81 once, then 5 % of
  the random cases; Libye1_mp left out, see below) or one made for the case (scene2.bin, actors.bin, sounds.bin,
  check2.bin or check.bin: objects of every kind with their own entries, the shifting textures, actors, the camera's
  settings, the shadows, lightmap groups, music, unknown entries, cut-short headers at a file's end, an empty
  scene2.bin). The world is cmd_diff's plus frames named as the mission's objects (their types from their entries,
  some missing, some under others, the lights in sectors already) and the scene's three sectors and its camera (or
  none). Then on each side: Begin, LoadScene (the scene's Open reporting progress through the loader's callback),
  LoadScene2, LoadActors, LoadSounds, LoadCheckpoints, or C_game::LoadMission, in each role. Compared: the results,
  every call into the game and the engine (the sector's environment vectors, the occluders' and areas' meshes by
  their contents), the loader's names (the frame map), the game's fields, the car scene, the particles' scene, the
  shifting textures' entries, the world's frames (names, types, links, the scene's, the dummies' boxes, the lens
  flares' and projectors' fields), the scene's +0x210, the files opened and closed.
- **The tests:** 3,000 cases and the 81 missions (about 3.3 million calls, 35 s; seeds 1, 2, 4). **Coverage:** every
  line but the shifting textures' length clamp (the port's fix) and the camera entry's unreadable header (both
  programs loop for ever).
- **Port bugs found:** none.
- **The original's weaknesses (kept, or the port's fixes):**
  - Names are used as printf formats (the frames' names, the sectors', the shifting textures' and the loader's): the
    port takes them as text (its fix). The game's own data has one: Libye1_mp's "AMB%BACK" is filed by the original
    under a mangled name and not found by its own name afterwards (left out of the test).
  - A shifting texture's name over 255 bytes is written past the original's buffer (the port's fix keeps it).
  - Entries read short keep the original's stack values (the port's zeros); the shadow parameters' colour w and their
    fourth unread word (+0x0C, +0x1C) are never set (left out of the comparison).
  - The camera entry of an object skips a header that does not read and loops for ever; an actor's part of another
    kind is not left (the next parts are read inside it; a file of them overflows the original's chunk stack, a
    crash); neither is given.
  - The projector's SetOrthogonal is passed the byte with the original's stack above it (the low byte compared).
- **Harness lessons (my mistakes):** the shared Compare skips a case whose original wrote past a block, silently:
  SetMission's static string, released by Begin, made every case skip and the first runs (and a mutation run) compare
  nothing; the test now builds the original's strings on its heap and fails on an overrun. The frames' type masks are
  1 << (type - 1) (my EnumFrames answers took 1 << type; mod_diff's does too). A light's entry sizes must follow the
  scene's version only where the version is certain. The made world shared the cut-short tails' names, so they made
  no frames of their own at first; typed objects took existing names (no frame made) until they had fresh ones.

## Air attack part 1 (the strafing fighter planes, `air_attack.cpp`, 2026-11-25)

- **The build:** `script/build_aa.bat` (`script/aa_diff.cpp`, suite `air.attack`), new: `cmd_diff.cpp`'s world, fakes and
  logger; the planes' calls through `aa_calls.s` / `aa_calls.inc` (`gen_cmd_calls.py aa`; the collision world's
  `SetIgnoredVolume` and `RayTestDynamic` and the camera module's `GetCameraPos` added to the table). The sound cache runs
  on both sides; the LS3DF math is LS3DF.dll's on both sides (`SetDir3`, `GetScale`, the vector's `RotateByMatrix` by
  their exports); `RandomFloat` and `AngleBetween2D` the original's; a script's `OnDeath` logged on both sides (the
  port's patched at run time, the original's redirected); a human's sector (the original's 0x005ac4c0 on its body)
  logged on both.
- **The cases:** one to three planes (models under the primary sector with their blades, wheels, flashes, propeller,
  some missing; odd headings: tiny, zero), made on each side (the original's by its constructor 0x005e3c90), given
  properties, started; then updates, ticks, hits, SetTarget (humans' frames, other frames, none), saves (an entry
  written after them) and loads (into the plane or another: its own file, made files with every entry, a target by name, bullets; the plane placed
  near its target as the base's load would), restarts, stops and starts, FindAirAttackFor, PauseAirAttackSounds,
  RayTestPastBorders, WriteProperties, SetFrame again. The rays answer nothing, a border, sand, a minefield, another
  material; the target's frame or a plane's volume now and then; normals level, up, slanted, along or across the ray.
  Compared after each: the answer, every plane's fields and lists (bullets, flashes, impact and whizz sounds, dust),
  the planes' list; then the frames and every call.
- **The tests:** 1,500 cases (about 680,000 calls, 17 s; seeds 1, 2, 4, 6). **Coverage:** every line but the dust's
  deletion when a shot-down plane passes 500 m (a sequence the cases never made) and the levelling's clamp for a
  falling direction (unreachable in both programs).
- **Port bugs found:** the ahead ray's distance tested `>= clearance` for levelling, so a NaN distance climbed where
  the original levels (it tests `< clearance` for climbing).
- **The original's weaknesses:**
  - Its Fire divides by the flash count (a plane model without flashes; the port's fix fires none); a stop keeps the
    released motor and gun sounds' pointers (an update or a pause after it calls through them; the port's fix clears
    them); a short property entry takes an uninitialised local (the port's fix keeps the value). Not given to the
    original (the test's planes have flashes, stopped planes are not updated, no short entries).
  - Both programs: an approaching plane without its target reads it through null (a save whose target is gone; the
    made saves give state 1 only with a target that is there); a direction straight up divides by 0 in the levelling
    (NaN: the test stops updating such a plane); the levelling of a falling direction steepens it.
  - Stack words: the smoke's matrix's w column, StartMission's box centre w, the bullets' w (left out).
- **Harness lessons (my mistakes):** an update of a never-started plane fired through an empty flash list (crashing
  the original) until stopped planes were left alone; the x87 rounds a denormal twice where SSE rounds once, so
  numbers under 1e-30 are compared as 0 (a plane flying straight up made them); the save's chunk must be entered
  before LoadGame (the actors module does it), else nothing loaded; the first runs never fired (no plane near its
  target in sight), found by the coverage.

## Heavy fire support part 1 (the air attacks and artillery strikes, `heavy_fire_support.cpp`, 2026-11-25)

- **The build:** `script/build_hf.bat` (`script/hf_diff.cpp`, suite `heavy.fire`), new: `cmd_diff.cpp`'s world, fakes and
  logger; the module's calls through `hf_calls.s` / `hf_calls.inc` (`gen_cmd_calls.py hf`; the sound module's `Stop` and
  `GetSound` added to the table). The model and sound caches run on both sides. `heavy_fire_support.cpp` is linked first:
  its global module reads the table as the program starts, after the test's file fakes are made. The module base's
  constructor is the test's (name and id), not logged (the port's global is made before the logger).
- **The cases:** a table (`tables\airsupport.tbl`: up to 16 kinds, their models and values, unknown entries; now and then
  cut short inside an entry's header), the module made on each side (the original's by its constructor 0x00471fc0) and
  started, then in the case's role: air attack requests (teams 0 and 1, kinds in the table and past it, 1..3 planes, a
  caller always), artillery requests, updates, ticks (odd frame times too), the server's messages (air attacks and
  artillery with made contents: kinds in the table, callers by id or none), starts and stops. Compared after each: the
  answer, the kinds, the busy flags, every strike's fields and planes; then the world's frames and every call (the
  explosions', the sound requests' contents, the radio's file names).
- **The tests:** 1,500 cases (about 270,000 calls, 6 s; seeds 1, 2, 4). **Coverage:** every line.
- **Port bugs found:** `RequestArtillery` answered "not the server" for a request sent and for a busy team (the original
  0 and 1; the header said it always answered that).
- **The original's weaknesses (the port's fixes; not given to the original):** a request without a caller reads the
  caller's id through null (the port sends -1); teams past 0 and 1 write past the busy flags, the clients' artillery
  (team 2) the byte after the module; a kind past the table in a message reads past it; a message without planes reads
  the first of none; a caller id without an actor reads its type through null; values before a kind's model and kinds
  past 16 write outside the table; a value cut short reads its bytes there (the port reads none). Stack words: the
  strikes' targets' and the explosions' w, the sound requests' w (left out of the comparison).
- **Harness lessons (my mistakes):** the module's `C_Module` part takes `this` + 4 in the original (Update, Tick,
  Start/StopMission, OnMessage); g++'s vectors are 12 bytes, so the port's later fields are not at the original's
  offsets (read by member); the original's `dtaSeek` / `dtaWrite` must be redirected for its table reads; a backslash
  in a Bash heredoc collapsed in `build_hf.bat` again.

## Submarine part 1 (the submarines, `submarine.cpp`, 2026-11-25)

- **The build:** `script/build_sb.bat` (`script/sb_diff.cpp`, suite `submarine`), new: `cmd_diff.cpp`'s world, fakes and
  logger; the submarine's calls through `sb_calls.s` / `sb_calls.inc` (`gen_cmd_calls.py sb`). The pilot seat's camera
  keeper (the camera module's, tested with it) is logged on both sides (its constructor 0x00465f00, Restore 0x00466010
  and Update 0x00466110 redirected in the original); so are the game's texts.
- **The cases:** one or two submarines on each side (the original's by its constructor 0x005df000), models under the
  primary sector with their body, seats (user properties, camera dummies), propeller and hull boxes (dummies with boxes
  off-centre, visuals after one), some missing; odd orientations (tiny and zero axes). In the case's role: updates and
  ticks (the game's time advancing), the controls, the seats' virtuals (ExitSeat mostly with someone seated first),
  FindExit, saves (an entry written after them) and loads (the own file, made ones: another id entered, the climb
  first), restarts, stops and starts; every third case's submarines destroyed without a stop. The sweeps and the ground
  answer by turns; each sound slot has its own sound. Compared after each operation: the answer, its calls, every
  submarine's fields, boxes, seats and sounds, the body, the model and the humans; at the end the world's frames.
- **The tests:** 1,500 cases (about 535,000 calls, 42 s; seeds 0, 1, 2, 4). **Coverage:** every line but the null
  checks of a frame or a box (none is made) and an exit within 1e-4 m of the seat (the facing's tiny branches).
- **Port bugs found:** the water sounds' speed was 2.4f; the original compares with the double 0x4003333338000000, a
  little under it, so a speed of 2.4f itself is fast (fixed: the double, compared as a double).
- **The original's weaknesses (the port's fixes, not given):** without a pilot seat the camera dummy is read through
  null; a short entry in a load stores an unset local, a short version entry goes on with a stack value; the first
  hull box that is not a dummy takes an unset centre. Stack words: the body's w without a body, the exit's w's, the
  sound requests' w (left out of the comparison).
- **Harness lessons (my mistakes):** a frame's SetPos answer must make its children's world matrices stale (the
  engine's does); one sound for every slot hid the slots' mix-ups; a test that closes its file right after the save
  cannot see a chunk the save leaves open, nor one that stops every submarine the destructor's own reset.

## Junkers part 1 (the Junkers 88 bomber, `junkers88.cpp`, 2026-11-25)

- **The build:** `script/build_jk.bat` (`script/jk_diff.cpp`, suite `junkers`), new: `cmd_diff.cpp`'s world, fakes and
  logger; the bomber's calls through `jk_calls.s` / `jk_calls.inc` (`gen_cmd_calls.py jk`). The model, sound and
  animation caches run on both sides; the seat weapons are the test's fakes on both sides (their answers logged); the
  LS3DF math is LS3DF.dll's; `RandomFloat` and `AngleBetween2D` the original's; the texts logged.
- **The cases:** the bomber's model (its parts "<name>", "<name>d", "<name>f", some missing, children on some; the blur,
  propellers, BODY, the five seats' dummies with user properties; the fighters' and the paths' models opened by the
  engine by their file names), a bomber on each side (the original's by its constructor 0x005ef180), properties,
  started; updates and ticks (odd frame times, the stages' time edges), hits on the fighters, SetState, the seats'
  virtuals, FindTarget, the paths' notes, saves and loads (own, the case's previous one, made ones with the engines'
  list), restarts, stops. Compared after each: the answer, every field, part, fighter, debris, tracer, path and chatter,
  the volumes, the input and HUD bytes; then the frames (scale too) and every call (the sound files by name).
- **The tests:** 1,500 cases (about 3.6 million calls; 2.5 min built and run under load; seeds 0, 1, 2, 4).
  **Coverage:** every line but HasNext's stage 0 (unreachable), long or empty debris names and a debris without its
  frame (the port's fixes), no tracer model, the model files failing to open and a fighter without guns (the original
  crashes afterwards), two lines of the loads.
- **Port bugs found:** a sound request's source; S_Path's owner update and its second GetChild; Hit's controller type;
  the fighter's down-check order; the debris name's terminator (0xFD) and its 16-bit length.
- **The original's weaknesses (the port's fixes, not given):** a debris with a null frame after a load; the group-4
  chatter divides by 0; S_Path::Save through a null model; a missing fighter model calls through null in Update; the
  camera path without its model; long debris names read onto its stack; EndAttack's StopAttack without a null check (a
  fix in the port without a "Fix:" mark).
- **Harness lessons (my mistakes):** LinkTo must refuse cycles (seed 1 overflowed the stack); loads of the case's
  previous save reach more than its own file; the sound files must be logged by name; a classification agent's claim
  that 15 survivors die under the default seed did not hold when they were run again against the final test.

## Train part 1 (the trains and their track player, `train.cpp`, `track.cpp`, 2026-11-25)

- **The build:** `script/build_tr.bat` (`script/tr_diff.cpp`, suite `train`), new: `cmd_diff.cpp`'s world, fakes and
  logger; the trains' calls through `tr_calls.s` / `tr_calls.inc` (`gen_cmd_calls.py tr`; the sound module's
  `RegisterSoundFrame`, `S_SoundFrame::SetSectorFactor` and `S_Checkpoint::GetLink` added to the table). `track.cpp` is
  linked (the port's track player on the port's side); the original's tracks are built by its own AddKey (0x00402880),
  FindTrack redirected; the LS3DF math (GetDir, Slerp) LS3DF.dll's.
- **The cases:** trains on each side (the original's by its constructor), their wagons (bodies, crossheads, connecting
  rods, dummies, some frameless), tracks of keys (repeated or near keys, an empty name, a first entry not the version),
  checkpoints; updates, SetTrack, whistles, Move, saves and loads (made ones too; NaN bytes normalised), restarts,
  stops and starts, the creator. Compared after each: the answer, every field, wagon, place and path entry, the frames
  and every call.
- **The tests:** 1,500 cases (about 1.5 million calls, 61 s built and run under load; seeds 0, 1, 2, 4). **Coverage:**
  `train.cpp` every line but tiny headings (81, 83), a long track name (435-441: the original reads it onto its stack)
  and three unreachable lines; `track.cpp`'s key functions every line (its path and spline player are not tested here).
- **Port bugs found:** PlaceWagon's sway rounded twice (sin in doubles, one rounding to float as the original);
  UpdateWagonVolume's transforms used `operator*` where the original uses `*=` (LS3DF rounds the last bit apart);
  `track.cpp`'s TimeToKey, AccelerateTowardsKey and FindPointAtDistance computed in doubles and other orders (now
  float in the original's order, KeyDistance / KeyNormalize; a probe matched all four bit for bit).
- **The original's weaknesses (the port's fixes, not given):** null reads when saving a running train without a track
  or with a frameless wagon, when updating or moving without the first wagon's frame, moving on a track of fewer than
  2 keys, SetTrack 'on' with an unknown name; places past the wagons; a long track name read onto its stack; unset
  locals in short entries.
- **Harness lessons (my mistakes):** the original's track key array needs its growth field (+0x1C) set; NaN save bytes
  differ in sign between the x87 and SSE (normalised); the checkpoints' Get reads only the index's low word.

## Seat weapon part 1 (the weapons at vehicles' seats, `seat_weapon.cpp`, 2026-11-25)

- **The build:** `script/build_sw.bat` (`script/sw_diff.cpp`, suite `seat.weapon.orig`; the vehicle harness's
  `build_sw.bat` self-check stays), new: `cmd_diff.cpp`'s world, fakes and logger; the weapon's calls through
  `sw_calls.s` / `sw_calls.inc` (`gen_cmd_calls.py sw`; the items module, the weapon item's PlayShotSound, the shot
  manager's FireBulletFrom / FireProjectile, the physics module, the net replication and messages, the human's view and
  disguise, the car's simulation and the vehicle's ApplyImpulse added to the table). The model, sound and animation
  caches run on both sides; BuildBasis is the original's on both; LS3DF's quaternion and AngleTo by their exports. The
  frames' answers turn them for real: SetWorldDir sets the rotation (the parent's turn taken out), Update makes the
  children's world matrices stale. Items, definitions and components are built in each side's layout with logged
  vtables; the sound groups likewise (the original reads a group's samples at +4, +0xC tested for none).
- **The cases:** a weapon model per case (per seat HORT / VERT / AIM, barrels with SHDUM / CARDUM and the item, rates
  and limits in their user properties, VIEW, a projectile, the seat dummy; parts missing), one or two weapons on each
  side (the original's by its constructor 0x004c19d0) on a car, a static weapon or another actor, in every role:
  Init (without model or seat frame too), Stop, Restart, SetGunner, ShowView, updates and ticks, the controls,
  FireShot, PlayShot, Reload, ShowReload, SetRounds, the getters and CanAim (targets above the turret too), saves and
  loads (own, previous, made), the barrels' notes (a shell thrown), SeatWeaponSendMessage, MakeSeatWeaponMessage and
  the messages' Write and Read. A gunner is the local player or not for a whole case; his view mostly stays.
  Compared after each: the answer, its calls (the physics shell's description, the view's w, the messages' contents),
  every field and barrel, the item's rounds; at the end the frames.
- **The tests:** 1,500 cases (about 110,000 calls, 7 s; seeds 0, 1, 2, 4). **Coverage:** every line but the port's
  fixes (the destructor's releases, a muzzle-less barrel's animation, no SHELL, no net object) and two unreachable
  returns.
- **Port bugs found:** none.
- **The original's weaknesses (the port's fixes, not given):** listed in the header's "Fix:" notes: a failed weapon's
  item, animations and shells leaked; an item without a component read through null; FireShot's barrel unchecked; no
  net object queued through null; a gunnerless reload's role read through null; a short rounds chunk's unset value.
  Stack words: the aimed shot's direction w, the reload sound request's w (left out).
- **Harness lessons (my mistakes):** a relative `build_sw.bat` path did not build, so a fix I thought tested had not
  run; the case's owner fell back to a non-car actor while the test still wrote a car's mass into its smaller block
  (a heap corruption that crashed only in some builds); the original's sound group layout is not the header's.

## Static weapon part 1 (the guns on mounts, `static_weapon.cpp`, 2026-11-25)

- **The build:** `script/build_st.bat` (`script/st_diff.cpp`, suite `static.weapon.orig`), new: it includes
  `sw_diff.cpp` (`SW_DIFF_NO_MAIN`: its world, frame answers, items, sound groups and weapon dumps); the weapon's calls
  through `st_calls.s` / `st_calls.inc` (`gen_cmd_calls.py st`; the actor messages, the items' save and load, the hit
  zones, PopMessage, CreateItem, the human's NotifyShotAt, SetFacingDir and IsFriendOf, FindActorPoint added). The seat
  weapon inside is each side's own; a script's OnUse logged on both sides (the port's patched, the original's
  redirected); the texts logged; the item definitions get a logged GetAmmoItemId; the store's containers answer in
  each side's layout (the original's GetSlots: pointers to the slots' vectors). The net object's setters are the
  test's on the port's side (the original inlines them).
- **The cases:** the weapon's model (seats with user properties and cameras under them or their parents, seat 0's
  turret parts, visuals with boxes, some flagged; the destroyed model "C<file>" or none; a model that does not reopen),
  a weapon on each side (the original's by its constructor 0x00602ec0), its store and net object; in every role:
  StartMission, StopMission, RestartMission, SetState, updates and ticks (with the net object's messages: shots,
  reloads, rounds, ammunition), OnMessage, WriteState / ReadState, saves (an entry after them) and loads, the seats'
  and the seat weapon's virtuals, Hit (tiny directions too), GetBoxCorners, the exit, the creator. Compared after each:
  the answer, its calls, the weapon's fields (box, seats, the gunners' places, state), the net object's settings and
  the seat weapon's every field; at the end the frames.
- **The tests:** 1,500 cases (about 450,000 calls, 10.5 s; seeds 0, 1, 2, 4, 6). **Coverage:** every line but
  GetWeaponBox for a frame that is itself an object visual (the weapon's frame is always a model).
- **Port bugs found:** GetBoxCorners transformed the corners with `operator*`; the original uses `*=` (LS3DF rounds the
  last bit apart).
- **The original's weaknesses (the port's fixes, not given):** a weapon without its item reads it through null at the
  start and in the refills; an update without seats reads the first seat's gunner through null; a seat out of range
  in the controls reads past the seats; a camera search from a seat dummy without a parent; an empty box's centre
  unset. Stack words: the exit's w (FindExit; left out).
- **Harness lessons (my mistakes):** a misaligned bit queue sent the store's read loop (until a 0xFFFF uid) through
  random answers for minutes (91 million calls): the queue is now built per message kind and answers all ones when
  read out; the actor name is at +0x80, not +0x6C; message names are by address and a freed message's address comes
  back, so popped messages go unnamed.

## Sight part 1 (the sight module, `sight.cpp`, 2026-11-25)

- **The build:** `script/build_sg.bat` (`script/sg_diff.cpp`, suite `sight.orig`), new: `cmd_diff.cpp`'s world, fakes and
  logger; the module's calls through `sg_calls.s` / `sg_calls.inc` (`gen_cmd_calls.py sg`; the humans' and the melee
  trackers' view tests and sight events, the collision world's SetIgnoredVolumes added). The original's module is its
  global (0x0089b288: its own functions read it), built once by its constructor and cleared for each case; its CRT's
  atexit is redirected (GetVisibleParts registers a static's destructor).
- **The cases:** cmd_diff's humans and melee trackers, their frames given the body parts' collision volumes (some
  missing, some in capitals), the trackers their body frames and body volumes; AddObserver and AddTarget (mostly from a
  few trackers' and humans' frames, so that one actor is observer and target; other actors' frames, a frame without an
  actor, none; masks 0..16), RemoveObserver, RemoveTarget, SetObserverState, SetTargetState, TestNow, CanSee,
  GetVisibleParts, a target's Refresh, its testedBy bits, ticks, the mission calls, walls (every ray blocked, the views
  100: what was seen is lost); the trackers' view test writes nothing now and then. Compared after each: the answer, its calls and the whole module (every target's and
  observer's fields, records and parts, the cursors, the start delay, the sight time).
- **The tests:** 1,500 cases (about 54,000 calls, 4 s; seeds 0, 1, 2, 4, 6). **Coverage:** every line but a human
  record's positive view (never set: only the trackers' views are), a tracker without a body frame (the port's fix),
  the destructor, a null observer in AddObserver's search and a target frame without an actor (unreachable).
- **Port bugs found:** the trackers' part test kept the wait it had worked out; the original passes the wait as the
  view test's status, so the next test is due at the mission time plus that status (kept, as a quirk); AddObserver
  skipped the relation's IsFriendOf / IsHostileTo calls for the observer's own actor (the original makes them, then
  takes it as a friend).
- **The original's weaknesses (the port's fixes, not given):** its inlined GetTarget reads a null target's mask (a
  frame of no human or tracker registers a null target): SetTargetState and TestNow crash on one (the port's fix, now
  marked); CanSee and GetVisibleParts read a null or removed target's count and parts; a human observer's update takes
  a removed target's actor through null; a tracker without a body frame reads it through null. Both programs read past
  an observer's records when they fall out of step (AddObserver skips a null target's record, AddTarget an observer
  of mask 0), the tick's reverse test included (not given).
- **Harness lessons (my mistakes):** the first coverage run measured 20 lines: the coverage build crashed in the
  original's CRT atexit (not redirected) and nothing was written; one actor must be observer and target for the tick's
  pairs to be tested at all; a backslash in a Bash heredoc collapsed again (in the staging script).

## Track part 1 (the track module and its spline players, `track.cpp`, 2026-11-26)

- **The build:** `script/build_tk.bat` (`script/tk_diff.cpp`, suite `track`), new: `cmd_diff.cpp`'s world, fakes and
  logger; the module's calls through `tk_calls.s` / `tk_calls.inc` (`gen_cmd_calls.py tk`). The original's module is
  its global (0x00867a64), built by its constructor; its CRT's atexit, sprintf, aligned allocations and rw_data's
  write, seek and open-for-write are redirected.
- **The cases:** the game missions' `tracks.dat` (17 of them), a made one (bending lines of keys, now and then a hair
  apart, speeds left out, rotations negated) or none; LoadMissionData / FreeMissionData, AddTrack, FindTrack,
  CreatePlayer (frames, parents, none), RemovePlayer, the module's Update in each game state, Stop / Restart, saves
  and loads (the file just written, the case's previous one, or a made one: fields left out or unknown, two tracks in
  one player's chunk, a count one short, long names), GetOffsetFromStart, AdvanceAlongKeys, BuildPath. Compared after
  each: the answer, its calls (the frames' places and rotations) and the whole module (every track's keys, every
  player's fields and its path up to the end mark), the reader's place after a load.
- **The tests:** 1,500 cases (about 55,000 calls, 17 s; seeds 0, 1, 2, 4, 6). **Coverage:** every line but a player
  past its track's end (see below), the unknown entries (the port's fix), the destructor; the interpolation helpers
  have no callers.
- **Port bugs found:** the path code's lengths and normalisation were the SDK's methods; the original's are inline,
  their squares summed z, y, x (SubdivideBezier's areas y, z, x and z, x, y), and AccelerateTowardsPathPoint groups its
  sum otherwise; a NaN tolerance (the original's 0.0001), MoveAlongPath's NaN tests and a NaN speed at the path's end
  (the original returns the time) were the other way round.
- **The original's weaknesses (not given):** a player whose update runs past its track's end: Advance builds the
  path of the last key, reading the key after the end (garbage, now and then a crash, the cursor running on); the
  test runs Advance's steps on a copy in the port's code first and leaves such updates out (so a player never turns
  inactive: only that path does it). A load of a player whose key has no next one on a shorter track of the name, or
  of a track without keys, reads past the keys too (left out). The constructor places the frame at the start before
  setting it (the port's fix: the first key; masked). The loader does not leave unknown entries (the port's fix, now
  marked); past the last player a failed Descend sends it back to the last field's case (made saves never count more
  players than they hold).
- **Harness lessons (my mistakes):** zeroing the players' paths by hand hid a mutant: both programs' allocations are
  zeroed already; a mutation run copied the harness while I edited it and reported 61 kills of a broken test (rerun
  cleanly); a Bash heredoc collapsed a backslash again.
