# COOP 005 Preparation state and loadout transactions

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-005-preparation-model`
- Base commit: not started
- Dependencies: COOP-002
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

Implement the preparation model behind both the new UI and network adapters: ownership
permissions, committed soldier loadouts, and atomic changes to campaign stock. Preserve
singleplayer equipment rules while allowing simultaneous human editors.

## Files and shared contracts

Own new preparation implementation and focused tests. Consume COOP-002 request/result and
stock contracts. Audit existing item/soldier/equipment validation for reuse; coordinate any
changes to `equipment_screen.*` or `equipment_panel.*` with COOP-007. Prefer adapters over
an unrelated rewrite of the original singleplayer screen.

## Acceptance criteria

- Each human edits their own soldier; host edits their own and unclaimed AI soldiers only.
- Commit each edit atomically across loadout and stock. Concurrent requests for the last
  item cannot duplicate it or overwrite another soldier's state.
- Test stale/repeated requests, rejected/invalid equipment, bulk auto-equip permissions,
  ownership changes while editing, and disconnect without double returns to stock.
- Preserve equipment capacity and mission-essential checks. Implement revision/freeze and
  readiness semantics only as agreed in the contract; flag unresolved deployment policy.
- Expose a UI/transport-independent API and tests. No claim of a connected preparation
  screen is made until its later live synchronization gate passes.

## Handoff

- Completed: proposed transaction rules in the campaign design.
- Remaining: reviewed transaction contract, implementation, and behavioral tests.
- Next action: read the merged COOP-002 contract and inventory reusable equipment validators.
- Blockers: COOP-002 has not merged; deploy/stock UX policies may need user input later.
- Decisions: editing permissions are confirmed; shared-state implementation follows the contract.
- Validation: none for this task.
