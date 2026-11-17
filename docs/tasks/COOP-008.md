# COOP 008 Controller and input adapters

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-008-controller-adapters`
- Base commit: not started
- Dependencies: COOP-001, COOP-004, COOP-006
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

Adapt controller selection, player input routing, and disconnect handling to explicit
soldier ownership. Finish and test these adapters behind an inactive campaign-session
boundary before COOP-010 enables the first playable development path.

## Files and shared contracts

Expected hotspots: `human_control.cpp`, `player_net.cpp`, `player_controller.cpp`, `team.cpp`,
`net_players.cpp`, and controller handlers. `game_rounds.cpp`, shared profile/header changes,
and actor creation overlap COOP-009; the coordinator must assign exact edits before starting.
Use the COOP-002 coupling audit to split this task further if needed.

## Acceptance criteria

- Player input resolves the controlled soldier independently of actor/player-slot equality.
- Ownership transfer updates host/guest controllers, input handlers and local camera/view
  targets while preserving soldier state. Stale input loses authority after transfer.
- Disconnect preserves the soldier/equipment and returns eligible soldiers to squad AI.
- Audit scoring/class, activity, inventory and damage paths that index players by actor ID.
  Campaign behavior preserves soldier skills rather than assigning multiplayer ranks.
- Build and run focused adapter/harness regressions for local and native co-op paths.
  Live behavior is enabled and demonstrated jointly with lifecycle work in COOP-010.

## Handoff

- Completed: high-level identity coupling identified in the campaign design.
- Remaining: exact file split, adapters, handoff/input tests, legacy regression checks.
- Next action: review merged dependencies and agree on overlapping-file edits with COOP-009.
- Blockers: dependencies have not merged; runtime prerequisites tracked by COOP-001.
- Decisions: preserve soldier identity and explicit simulation authority.
- Validation: none for this task.
