# COOP 002 Shared campaign co-op contracts

## Coordination

- Status: active
- Owner/session: Codex COOP-002 / `01a1ebd2-902d-7e32-9876-9818a845d361` (2026-11-18)
- Canonical branch: `feature/coop-002-contracts`
- Base commit: `7e033adde95881baad06c6877cbbd6eef8197d96`
- Dependencies: none
- Last checkpoint: initial claim commit; this branch `HEAD`
- Checkpoint location: `.worktrees/COOP-002`; `origin/feature/coop-002-contracts` after push
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

Define the shared contracts that let ownership, preparation, networking, and UI develop
independently. Audit coupling in legacy code and choose narrow adapters. This completes
an interface/design foundation; it does not claim to enable campaign co-op.

## Files and shared contracts

Own `docs/coop_contracts.md` and any agreed new shared headers in `src/game/` and `src/net/`.
Inspect `profile.h`, `human_control.cpp`, `player_net.cpp`, `spawn_point.cpp`, `game_app.cpp`,
`game_rounds.cpp`, equipment code, and script player-team queries. Coordinate names and
types with COOP-003; refine COOP-004 through COOP-010 file scopes before marking them ready.

## Acceptance criteria

- Define session/mission/preparation identity, soldier identity, player ownership, revision
  and generation semantics, permitted transitions, requests/results, and snapshot boundaries.
- Specify equipment permissions: human own soldier; host own and unclaimed AI soldiers.
  Define a UI-facing preparation interface, so the UI can use a fake implementation.
- Define protocol compatibility negotiation and rejection semantics without assuming wire
  layouts or legacy object IDs can change freely.
- Map actor-ID/player-slot assumptions and local/network rule branches to adapter tasks.
- Specify invariants and behavioral tests for downstream modules. Compile any new headers
  in their intended target environment; document-level contracts have an explicit review.
- Keep unconfirmed product decisions visible and isolate them from independent interfaces.

## Handoff

- Completed: required design/workflow/task reads; fetched origin and inspected branches, worktrees and open PRs. Canonical branch/worktree created from integrated `main`. Claim acknowledged by coordinator ledger commit `c5a2f841`.
- Remaining: concrete interfaces, coupling audit, consumer review, downstream scope refinement.
- Next action: write document-level identity/preparation/protocol contracts, source coupling audit and exact downstream file scopes. COOP-003 consumer coordination is active through its task thread; no new headers or gameplay changes are planned.
- Blockers: none for initial contract work; unresolved product choices are in the design.
- Decisions: see confirmed behavior in `docs/base_game_coop.md`; proposed defaults stay proposals.
- Validation: none for this task.
