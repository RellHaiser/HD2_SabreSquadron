# COOP 009 Campaign mission lifecycle adapters

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-009-campaign-adapters`
- Base commit: not started
- Dependencies: COOP-001, COOP-004, COOP-005, COOP-006
- Last checkpoint: none
- Checkpoint location: none
- Review/integration: none

## Result and scope

Implement original campaign spawning and lifecycle adapters for a network session,
consuming committed squad/loadout state and preserving campaign rules. Keep them dormant
outside the explicit campaign-session boundary until COOP-010 connects the development run.

## Files and shared contracts

Expected hotspots: `game_app.cpp`, `game.cpp`, `spawn_point.cpp`, `game_rounds.cpp`, profile
settings, and script player-team queries. Coordinate `game_rounds.cpp` and actor/controller
integration with COOP-008. Narrow/split scope from the COOP-002 audit before claiming.

## Acceptance criteria

- Spawn the selected squad at original campaign points with committed equipment and stats;
  avoid multiplayer zones, warmup, lives, respawns, ranks, and map rotation.
- Distinguish simulation authority from campaign rules without redefining local play.
- Campaign script player-team queries include all deployed human and AI soldiers.
- Prepare explicit initial-world synchronization/loading-barrier and campaign-result hooks;
  define their integration obligations without claiming full save/load or late join.
- Build and run focused lifecycle regressions for ordinary singleplayer/native co-op.
  The completed adapter scope and dormant activation must be reviewable without placeholders.

## Handoff

- Completed: legacy start/stop/spawn coupling mapped in the design.
- Remaining: exact file split, campaign adapters, synchronization hooks, focused regressions.
- Next action: review merged contracts/models and agree on overlapping edits with COOP-008.
- Blockers: dependencies have not merged.
- Decisions: original campaign spawning and mission behavior; multiplayer infrastructure reuse.
- Validation: none for this task.
