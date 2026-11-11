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

`mutrun.py`: it now takes the g++ line with the most `%SRC%` sources, because the equipment
bat compiles math.cpp first. Env `MUT_EXTRA_SRCS` / `MUT_EXTRA_INC` add sources from outside
`src/`, which are compiled once and not mutated. The equipment run:
`MUT_HARNESS=equipment MUT_EXTRA_SRCS=ls3df/src/math/math.cpp MUT_EXTRA_INC=ls3df/src`.
