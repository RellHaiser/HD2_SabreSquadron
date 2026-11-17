# COOP 011 Compiled shared campaign declarations

## Coordination

- Status: ready
- Owner/session: unassigned
- Canonical branch: `feature/coop-011-shared-declarations`
- Base commit: not started
- Dependencies: COOP-002 (document-level contracts merged as `9f59ff4790f02ef73e3e02c850f79cfa9ba95130`)
- Last checkpoint: none
- Checkpoint location: none
- Review/integration: none
- Pull request: none

## Branch authority and completion PR

The assigned agent has full control of this task's branch, including editing, committing,
rebasing, resolving conflicts, pushing to the project remote, and creating/updating its
PR without repeated user approval. Preserve other workers' work and shared checkpoints.
When implementation and required validation are complete, push and create/update a PR
targeting `main`. Follow the [completion PR requirements](../agent_workflow.md#review-and-integration).
Include reproducible test commands, prerequisites and expected results; distinguish
checks run from suggested reviewer checks. State **Source-only** if no runnable artifact
is supplied. Source-only delivery does not waive the target compile requirement.

## Result and scope

Translate the reviewed [COOP-002 logical contract](../coop_contracts.md) into a small
engine-independent shared header so ownership, preparation, codecs and fake UI consumers
can compile independently against one set of declarations. The coordinator approved this
separate foundation while COOP-002 refined downstream scopes. It replaces the initial
idea of having an implementation task publish common types as a side effect.

This task supplies declarations and a compile/fake-interface conformance fixture. It
implements no ownership transitions, equipment transactions, protocol codec, game adapter
or shipping UI. The downstream tasks retain those results and their acceptance gates.

## Files and shared contracts

Own `src/game/campaign_contracts.h` and `_re/harness/campaign_contracts/`, including a
documented compile command. Cover distinct identity value types, lifecycle/context views,
ownership records/requests/results, preparation operations/snapshots/events and the
`I_CampaignPreparation` boundary. Use the port's PascalCase methods and `S_` data names;
the logical interface spelling above is shared with COOP-003. Do not add engine pointers,
profile globals, Windows transport objects, packed legacy layouts or a speculative wire
ABI to these declarations. Wire widths/encoding remain COOP-006's responsibility.

Review names, snapshot lifetime, optional/invalid identities, request correlation and
construction needs with COOP-004/005/006/007 consumers. Keep ownership and preparation
implementations behind injected catalog/policy views. Shared header edits after merge
require notifying consumers; do not copy competing definitions into implementation files.

## Acceptance criteria

- Declare every shared identity with explicit scope/invalid semantics and type separation;
  do not permit implicit actor-ID/player-slot/SoldierId substitution.
- Express complete immutable snapshot and typed request/result/event boundaries from
  COOP-002, including separate preparation/view revisions, control tokens, capabilities,
  pending outcomes, resync and explicit unresolved-policy blockers.
- Compile a standalone translation unit using the intended MSVC x86 toolchain, independent
  of game data/engine DLLs, and record the exact compiler version/command/result. A g++
  syntax-only check can supplement but cannot replace this target check.
- Compile a minimal fake `I_CampaignPreparation` and representative DTO construction used
  by consumers; verify no writable campaign/profile object is exposed. This is interface
  conformance evidence, not a fake implementation claimed as production gameplay.
- Record explicit consumer/API review and preserve legacy engine/serialized structures.
  No runtime host/guest test is required for this declaration-only component.

## Validation evidence

None yet; this is a planned follow-up, not implementation delivered by COOP-002.
The target compiler requirement remains unmet until a worker runs it. Toolchain guidance
and availability evidence can be reused from COOP-001 without depending on its runtime
baseline result or treating unavailable tools as a passing compile.

## PR test instructions and delivery

- Delivery: declaration component; **Source-only** unless the later worker supplies an artifact.
- Prerequisites: intended MSVC x86 toolchain; game installation not needed for standalone headers.
- How to test: worker records exact compile/conformance commands after choosing the fixture.
- Validation actually run: none; no shared headers are introduced in COOP-002.
- Not run: target compilation and consumer conformance, pending claim/implementation.

## Handoff

- Completed: bounded declaration scope and canonical branch reserved by coordinator during COOP-002.
- Remaining: shared declarations, target compile fixture and consumer review.
- Next action: request a coordinator claim, then create the canonical branch/worktree from integrated main and implement the header/fixture.
- Blockers: none for claim; COOP-002 is integrated. Intended MSVC x86 compiler must be available before review; COOP-001 has established a local portable MSVC/Wine toolchain.
- Decisions: follow confirmed campaign behavior and COOP-002 engineering contracts; leave product policies explicit.
- Local state: no implementation branch/worktree exists for this planned task.
- Review notes: COOP-004/005/006/007 become implementation-ready only after this result merges (and COOP-003 for UI).
