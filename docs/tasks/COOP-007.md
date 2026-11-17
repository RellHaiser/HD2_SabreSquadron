# COOP 007 Campaign preparation screen component

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-007-preparation-screen`
- Base commit: not started
- Dependencies: COOP-002, COOP-003, COOP-011 (merged shared declarations)
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

Implement the reviewed preparation screen as a component against the shared UI-facing
model contract. Develop with a fake model so work can proceed alongside preparation and
network implementation. Production menu entry and live synchronization are later tasks.

## Files and shared contracts

Own new preparation-screen source/resources and UI harnesses. Adapt reusable campaign
equipment components where appropriate; coordinate shared component edits with COOP-005.
Do not wire a fake or incomplete screen into the normal mode picker. Keep assets original
or generated at install/build time; do not commit extracted game data.

### COOP-002 scope refinement

Contract implementation scope from COOP-002: own `src/game/campaign_preparation_screen.{h,cpp}`,
`_re/harness/campaign_preparation_ui/`, and original resources agreed by COOP-003.
The fake and live bindings share [I_CampaignPreparation](../coop_contracts.md#preparation-model-and-ui-boundary):
immutable snapshots, queued proposals/results, explicit synchronization/phase status,
revision-bound readiness and capabilities with denial reasons. Local editor selection
is not an ownership or squad mutation. Consume the merged COOP-011 interface for the
fake; no COOP-005 model implementation dependency is needed. Coordinate any `equipment_panel.*`/
`equipment_frames.*` extraction with COOP-005; keep `equipment_screen.*` on its solo route.

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
- Blockers: COOP-002, COOP-003 and COOP-011 have not merged.
- Decisions: new campaign-style UI, with existing singleplayer components reused when suitable.
- Validation: none for this task.
