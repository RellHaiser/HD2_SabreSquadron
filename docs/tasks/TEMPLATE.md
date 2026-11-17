# COOP ID Task title

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-NNN-short-description`
- Base commit: not started
- Dependencies: task IDs and the required merged results
- Last checkpoint: none; when committing this record, `HEAD` is the checkpoint if a
  literal self-referential commit ID cannot be recorded
- Checkpoint location: local only / actual remote branch or PR
- Review/integration: none; record the final integrated commit after merging
- Pull request: none; record its URL when created

## Branch authority and completion PR

The assigned agent has full control of this task's branch, including editing, committing,
rebasing, resolving conflicts, pushing to the project remote, and creating/updating its
PR without repeated user approval. Preserve other workers' work and shared checkpoints.
When implementation and required validation are complete, push and create/update a PR
targeting `main`. Follow the [completion PR requirements](../agent_workflow.md#review-and-integration).
The description must give reproducible **How to test** steps and expected results, report
checks actually run, and explicitly state **Source-only** or **Documentation-only** when
applicable. Source-only delivery does not waive required runtime acceptance evidence.

## Result and scope

Describe the concrete behavior or artifact this task completes. State what its consumer
can use and the boundary separating it from subsequent tasks.

## Files and shared contracts

List expected files, new files, and APIs/protocols/resources consumed or changed. Name
other tasks sharing integration files and the agreed edit/merge sequence. Update this
section before expanding scope.

## Acceptance criteria

- Define observable outcomes that make this deliverable complete.
- Include relevant regressions and failure cases, not only the happy path.
- State whether runtime host/guest evidence is required for this scope.

## Validation evidence

Record exact commands, environment/toolchain, tested commit, outcome, and concise evidence
or artifact location. Distinguish passed, failed, and unavailable checks. Keep game assets
and secrets out of committed logs. Documentation changes do not require a game build.

## PR test instructions and delivery

- Delivery: integrated gameplay / component; runnable artifact / Source-only / Documentation-only
- Prerequisites: toolchain, dependencies, game version/data, and relevant settings
- How to test: exact setup/build/launch steps or document/harness checks, with expected results
- Validation actually run: commands, environment, and outcomes
- Not run: missing checks and reasons; keep suggested reviewer steps distinct from passed checks

## Handoff

- Completed: none
- Remaining: concrete unfinished work
- Next action: one executable next step
- Blockers: dependency/decision/environment and what resolves it
- Decisions: confirmed user instruction or linked contract; label proposed defaults
- Local state: uncommitted files, generated artifacts, worktree, and required prerequisites
- Review notes: unresolved findings or comments

Update the handoff before interruption, reassignment, blocking, or review. A successor
must be able to resume from this record and the repository without the original chat.
