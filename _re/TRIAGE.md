# Triage of the unported game code (2026-11-05, after part 10f)

Regenerate: `python _re/tools/triage.py`, then `python _re/tools/triage_clusters.py`. One row per function: `_re/triage.json`.

## The numbers

| Group | Functions | Bytes | Meaning |
|---|---|---|---|
| Live | 826 | 216,295 | The real work |
| Tiny | 342 | 3,352 | Live, under 16 bytes: thunks and one-liners, mostly mechanical |
| Static | 6 | 163 | Named static initializers |
| Uncertain | 114 | 7,236 | Referenced only from code outside any known function, mostly exception cleanup |
| Dead | 278 | 175,650 | The original program cannot reach them |
| **Total** | **1,566** | **402,696** | |

Leaving dead code out, the port is at 93.6 % by size (3,451,260 of 3,685,638 bytes), up from the 89.4 % `port_stats.py` reports.

## How functions were classified

Reachability is worked out on the original program, from `WinMain` and the CRT's static initializers (the table at `0x00832004`).

- **Edges:** the decompile export plus a byte scan of `.text` (rel32 calls and jumps, absolute addresses). A chance byte match can only make a function look live, which is the safe direction.
- **Tables:** a vtable or callback list counts only when live code references it.
- **Orphan pointers:** a function pointer in data whose table no code references directly is taken as live (3 functions), since it may be a struct field.

**Validation:** the same analysis calls 277 already-ported functions (52 KB) dead. None has a reference from live code. Those were ported before this analysis existed, and they do no harm.

**Limits:**
- This is static analysis. Nothing was observed in a running game.
- "Live" means reachable, not exercised. A virtual slot in a live vtable may never be called.
- A runtime-computed address can't be ruled out, though nothing suggests one.

## The dead code: leave it in tier 1

| Range | Bytes | What |
|---|---|---|
| `0x716740`-`0x71ba0c` | 19,277 | `SphereTest2D`, `CollisionRayTestFraction`, ... |
| `0x70b590`-`0x70ff13` | 18,819 | `SweepSphereAabb2D` |
| `0x6f9520`-`0x6fce79` | 13,045 | `RayTestVolumeListInside`, `RayTestBoundingSphere`, ... |
| `0x712dd0`-`0x7158e0` | 10,443 | `SweepSpherePrimitive2D`, `SweepSphereCell2D` |
| `0x561740`-`0x5638a0` | 7,982 | Unnamed AI code; `FindPathDijkstra` (`0x51ec70`) is dead too |
| `0x6f12a0`-`0x6f298d` | 5,861 | Unnamed, beside the collision code |
| `0x6d1ef0`-`0x6d341e` | 5,422 | `MissionLoader_DeadScene2Reader` |
| `0x6fe110`-`0x6ff4e8` | 5,040 | Unnamed, beside the collision code |
| `0x56adb0`-`0x56be8d` | 4,317 | Unnamed, beside the AI code |
| `0x4b68e0`-`0x4b79b2` | 4,035 | `SubdivideRouteCurve`, `SmoothRoute`, ... |

The 2D collision family is dead as a whole: `SweepSphereAabb2D` is reached only through others in the family, whose chain ends at `0x71a080`, which nothing references.

## The live code, by cluster

"Expected" is how much of the cluster the hand-written source already names or declares.

| Cluster | Functions | Bytes | Expected | What |
|---|---|---|---|---|
| `0x5941d0`-`0x59bda5` | 52 | 28,211 | 7,069 | Formations: `C_FormationShape`, `PlanSlot`, `GetSlotTarget` |
| `0x5636d0`-`0x56adac` | 29 | 27,754 | 3,354 | Human AI movement: `C_HumanAIMemory`, checkpoints, called from `RouteToCheckpoint` / `RouteToPos` |
| `0x7966c0`-`0x79bd6b` | 41 | 19,939 | 5,727 | `C_HumanStains`, textures for hits |
| `0x6dd870`-`0x6e1cd5` | 42 | 15,265 | 4,947 | Timed events, `S_Explosion::Execute` |
| `0x6c5280`-`0x6c97a9` | 45 | 14,720 | 6,643 | `C_Controls::Update`, `BindControls`, `GameRenderCallback` |
| `0x56be90`-`0x56ee05` | 14 | 10,534 | 0 | More human AI, including `0x56c230` (7,264 bytes) |
| `0x424590`-`0x426fe2` | 4 | 9,638 | 9,608 | `C_ScriptModule::SaveGame`, `LoadGame` (vtable `0x80f750`, slots 12 and 13) |
| `0x4297b0`-`0x42bf55` | 23 | 6,710 | 3,653 | `C_ScriptModule` network state: `SendState`, `ReceiveState`, `HandleClientMessage` |
| `0x71e150`-`0x720025` | 24 | 6,407 | 6,097 | Speech slots |
| `0x48e9f0`-`0x4906f5` | 30 | 6,244 | 2,125 | `C_QuickMessages`, `C_TextBox` |
| `0x6e2d10`-`0x6e4f35` | 29 | 6,187 | 0 | Looks like crash reporting (`VirtualQuery`, `LoadLibraryA`, `GetModuleFileNameA`); inferred from the API calls, not read through |
| `0x7ce930`-`0x7cfa03` | 1 | 4,307 | 4,307 | `BuildPathPoints` |
| `0x6e9e70`-`0x6eaec5` | 15 | 3,424 | 500 | `GetExecutableMd5`, `InitProtectionTables`: looks like copy protection |

The other 145 clusters are small, about 57 KB in all. Across all clusters, 151 live functions (62 KB) are already named or declared by the hand-written source, and 467 (90 KB) have no unported live caller.

## Suggested order

1. **`C_ScriptModule` save, load and network state** (16 KB, mostly expected). Testable by save round trips.
2. **Speech slots and `BuildPathPoints`** (11 KB, nearly all expected).
3. **`C_Controls` and the render callback** (15 KB). A first run needs input.
4. **Timed events and explosions** (15 KB).
5. **Stains** (20 KB).
6. **Human AI movement and formations** (67 KB). The largest and hardest, so last.
7. **The small clusters**, then the first linked run.

## Decisions (owner, 2026-11-05)

- **Copy protection** (`0x6e9e70` range): replace with a pass-through.
- **Crash reporting** (`0x6e2d10` range): port it.
