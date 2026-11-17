# COOP 007 Campaign preparation screen component

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-007-preparation-screen`
- Base commit: not started
- Dependencies: COOP-002, COOP-003
- Last checkpoint: none
- Checkpoint location: none
- Review/integration: none

## Result and scope

Implement the reviewed preparation screen as a component against the shared UI-facing
model contract. Develop with a fake model so work can proceed alongside preparation and
network implementation. Production menu entry and live synchronization are later tasks.

## Files and shared contracts

Own new preparation-screen source/resources and UI harnesses. Adapt reusable campaign
equipment components where appropriate; coordinate shared component edits with COOP-005.
Do not wire a fake or incomplete screen into the normal mode picker. Keep assets original
or generated at install/build time; do not commit extracted game data.

## Acceptance criteria

- Render and navigate the campaign-style layout approved in COOP-003 through the model API.
- Show correct host/guest/AI edit rights and make read-only views unambiguously read-only.
- Exercise loading, rejected edits, changed ownership, scarce stock, readiness changes,
  keyboard/mouse input, and mission restrictions using representative fake states.
- Controls request validated transactions; they do not mutate shared squad/stock globals.
- Provide a runnable component preview/harness with visual/input evidence and document the
  live binding contract. Passing this task does not claim connected loadout selection.

## Handoff

- Completed: task scope only.
- Remaining: component implementation and UI verification after contract/layout merges.
- Next action: review the merged layout and preparation interface, then build the fake model.
- Blockers: COOP-002 and COOP-003 have not merged.
- Decisions: new campaign-style UI, with existing singleplayer components reused when suitable.
- Validation: none for this task.
