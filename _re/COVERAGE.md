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
