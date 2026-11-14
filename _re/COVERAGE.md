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
- **The tests (76,000, all exact):** CreateController, AssignPlayerController, SetScripted, StartScriptedController
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
  (equivalent: the unknown type's case).
- **Harness lessons (my mistakes):** the new controllers were named after their constructors logged (the names differed
  in the first run); the original's deleting destructor's flag was logged where the port has none; the other human was
  never made, so HandleMessage reached a zeroed object.
