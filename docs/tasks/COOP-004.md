# COOP 004 Session and soldier ownership model

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-004-ownership`
- Base commit: not started
- Dependencies: COOP-002, COOP-011 (merged shared declarations)
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

Implement the agreed session and soldier ownership model independently of live controllers
and transport. Consumers can resolve a player's soldier and authorize a control transfer
without treating the soldier's actor ID as a connection slot.

## Files and shared contracts

Own new ownership/session implementation files and focused tests at paths agreed in COOP-002.
Consume shared identity and transition contracts. Legacy controller and mission adapters
belong to COOP-008/009; avoid concurrent edits to those files.

### COOP-002 scope refinement

Contract implementation scope from COOP-002: own `src/game/campaign_session.{h,cpp}`,
`src/game/campaign_ownership.{h,cpp}`, and `_re/harness/campaign_ownership/`. Follow [the identity, lifecycle and ownership
contracts](../coop_contracts.md#identity-and-session-context), including connection
incarnations, per-soldier control revisions, script suspension and actor-binding invalidation.
Consume COOP-011 shared declarations; coordinate changes with all consumers before
editing that common header. Do not create independent incompatible copies.
No legacy controller/profile layout edits.

## Acceptance criteria

- Enforce one human owner per soldier, at most one soldier per player, and campaign squad limits.
- Handle claims, switching, disconnect, death, temporary scripted control, and mission reset
  according to the contract without changing a soldier's health, inventory, or identity.
- Reject invalid/duplicate/stale requests and ownership races. Verify mission-generation
  and ownership-revision boundaries with behavioral tests.
- Preserve ordinary singleplayer/native co-op behavior; no unfinished UI entry is exposed.
- Provide documented APIs and meaningful model tests. Runtime playability is not this
  module's deliverable; the live adapter and two-player gates remain separate.

## Handoff

- Completed: task scope only.
- Remaining: implementation and tests after contract merge.
- Next action: inspect the merged COOP-002 contract and agree on the exact implementation files.
- Blockers: COOP-002 and COOP-011 have not merged.
- Decisions: model follows confirmed campaign behavior and the reviewed contracts.
- Validation: none for this task.
