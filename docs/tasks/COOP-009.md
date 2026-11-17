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
- Pull request: none

## Branch authority and completion PR

The assigned agent has full control of this task's branch: editing, committing, rebasing,
resolving conflicts, pushing to the project remote, and creating/updating its PR without
repeated user approval. Preserve other workers' work and shared checkpoints. When the
implementation and required validation are complete, push and create/update a PR targeting
`main`. Include reproducible **How to test** steps, prerequisites, and expected results;
separate checks actually run from proposed reviewer checks. State **Source-only** explicitly
when no runnable artifact is supplied, or **Documentation-only** for documentation tasks.
Record the PR URL in this handoff. Follow the [completion PR requirements](../agent_workflow.md#review-and-integration);
source-only delivery does not waive required runtime acceptance evidence.

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
