# COOP 004 Session and soldier ownership model

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-004-ownership`
- Base commit: not started
- Dependencies: COOP-002
- Last checkpoint: none
- Checkpoint location: none
- Review/integration: none

## Result and scope

Implement the agreed session and soldier ownership model independently of live controllers
and transport. Consumers can resolve a player's soldier and authorize a control transfer
without treating the soldier's actor ID as a connection slot.

## Files and shared contracts

Own new ownership/session implementation files and focused tests at paths agreed in COOP-002.
Consume shared identity and transition contracts. Legacy controller and mission adapters
belong to COOP-008/009; avoid concurrent edits to those files.

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
- Blockers: COOP-002 has not merged.
- Decisions: model follows confirmed campaign behavior and the reviewed contracts.
- Validation: none for this task.
