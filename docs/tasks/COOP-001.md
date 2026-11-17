# COOP 001 Reproducible build and baseline

## Coordination

- Status: ready
- Owner/session: unassigned
- Canonical branch: `feature/coop-001-build-baseline`
- Base commit: not started
- Dependencies: none
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

Establish the runnable x86 build, its prerequisites, and a reproducible singleplayer/native
co-op baseline. Make concurrent builds safe by isolating outputs or documenting a verified
serialization procedure. Diagnose tooling before gameplay tasks depend on it.

## Files and shared contracts

Own build/run guidance and focused build-tool fixes in `_re/tools/` when needed. Inspect
the engine build scripts as consumers. Coordinate shared build changes with other workers;
do not port the engine or change gameplay as part of this task. Existing scripts use fixed
temporary directories, so verify cache separation across worktrees.

## Acceptance criteria

- Record exact toolchain, dependency, build, and launch commands for this environment.
- Build the playable executable and matching engine libraries with x86/x87 requirements.
- Record a solo mission run and a real host/guest native co-op baseline, with tested commits.
- Demonstrate that concurrent builds cannot consume another worktree's stale objects, or
  validate and document serialized builds until isolated paths are supported.
- Missing game data/toolchains are a blocker to the baseline gate, not a passing result.
  Split independently finished tooling improvements out if runtime access remains blocked.

## Handoff

- Completed: initial task scope only; no baseline run has been performed for this fork.
- Remaining: environment inventory, build, runtime baseline, output isolation evidence.
- Next action: inspect available toolchains/game installation and the upstream build scripts.
- Blockers: not yet investigated; do not assume the repository includes game data.
- Decisions: preserve the port's required runnable math/toolchain behavior.
- Validation: none for this task.
