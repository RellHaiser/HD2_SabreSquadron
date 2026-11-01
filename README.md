# Hidden & Dangerous 2: Sabre Squadron — C++ source port

A reconstruction of the game client of *Hidden & Dangerous 2: Sabre Squadron* (Illusion Softworks)
as readable, compilable C++, written from the decompiled 32-bit executable. The engine is LS3D; the
port keeps its structure, names and data formats so that it can stand in for the original code.

This repository holds no files of the game. Running anything built from it needs an installed copy
of the game and its data.

## Status

Work in progress (2026-10-31). Of the game's 12,293 functions, 7,963 (73.1 % of the code by size)
are ported by hand. Generated code (tier 1, below) covers the rest. The sources are
compile-checked file by file; there is no linked build of the game yet.

Ported so far, among others: the app core and module framework, the console, configuration and
data files (`C_chunk`), the mission script language (compiler, expressions, interpreter), the
campaign and profiles, humans (movement, animation, combat, items, AI, squads, enemies), the
interface and HUD, vehicles (setup, driving, damage, seats, the car actor, network and saved game),
seat weapons, shots, collision, the rigid body, debris, items lying in the world, destructible
scene objects, clocks, switches, photo places, breakable fences, radios, document places, mines,
timed explosions, spawn points, explosive places, pikes, flocks of birds, ladders, multiplayer
zones, submarines, trains, static weapons, bikes, schools of fish, doors, supply boxes,
barrels, air attacks, the Junkers 88, vertex animations, lightmap groups, texture shifting and the AI's
line of sight, and the graphic effects (screen fades, fading models, the thunderstorm, rain, the
underwater view, shadows, uniforms, shattered glass). In progress: the mission and world modules
(decorations, the camera, mission loading).

## Approach

- **Hand ports (`src/`, except `src/tier1`).** Each subsystem is rewritten from the decompilation
  and the assembly into C++ that follows the original's naming style: `C_Class`, `S_Struct`,
  PascalCase methods, `m_` members. Declarations carry the original address as a
  `// 0x00XXXXXX` comment.
- **Tier 1 (`src/tier1`).** A generated, mechanical translation of every function not yet ported
  by hand, so that the whole program is present while the hand port proceeds. It is regenerated as
  ports land, and compiles with a 32-bit g++ (`-fpermissive`).
- **Behaviour.** The original's behaviour is kept, including its oddities (commented
  `Quirk kept`). Plain crash and memory bugs are fixed (commented `Fix:`). Copy protection is
  dropped.
- **Validation.** Ported code is checked against the original's assembly and, for engine-heavy
  parts, run in test harnesses against stub engines and the game's own data (vehicle models,
  scripts, tables), with mutation testing of the tests. The reverse-engineering workspace, tools
  and harnesses are kept outside this repository.

## Layout

| Path | Contents |
|---|---|
| `src/common` | Strings (`C_str`), chunked data files (`C_chunk`), configuration files, file wrappers |
| `src/ls3d` | The LS3D engine interfaces the game uses: math, `I3D_*` (scene, frames, visuals), `IGraph`, `rw_data` |
| `src/game` | The game: app core and modules, actors (humans, vehicles, items, ...), AI, physics, interface, sound |
| `src/net` | The network bit stream and messages |
| `src/script` | The mission script language |
| `src/tier1` | Generated code (see above) |
| `docs` | [Mission scripting language reference](docs/HD2_Mission_Scripting_Language.md) |

## Building

The hand-ported sources target MSVC, x86, C++17 (Visual Studio 2026). Each file compiles on its
own with, from the repository root:

```
cl /nologo /c /std:c++17 /EHsc /W3 /permissive- /Zc:__cplusplus /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I src src\game\item_actor.cpp
```

Use an x86 developer prompt (`vcvars32.bat`). `src/tier1` is built with a 32-bit MinGW g++
(`-std=gnu++17 -m32 -fpermissive`) instead.

## Legal

This is an unofficial, non-commercial reconstruction for preservation and study, not affiliated
with the game's developers or publishers. *Hidden & Dangerous 2* and its content belong to their
respective owners.
