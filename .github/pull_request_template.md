## Result

Describe the completed behavior or artifact and why it is needed. State its scope if it
is a foundation/component rather than a playable feature.

## Delivery

State **Source-only** explicitly if no runnable artifact is supplied, or
**Documentation-only** for documentation changes. Explain whether code is integrated into
the game, whether a build/runtime test was performed, and where any supplied artifact is.

## Task and dependencies

- Task and handoff: `docs/tasks/COOP-NNN.md`
- Dependencies and their merged commits:
- Shared contracts/files affected:

## How to test

Give reproducible steps with prerequisites (toolchain, dependencies, game version/data),
exact setup/build/launch commands or document/harness checks, mission/settings, and
player count plus host/guest actions where relevant. State expected results. Source-only
delivery still needs instructions to build/test its source; documentation-only delivery
needs review/link/diff checks rather than gameplay steps.

1. Prerequisites and setup:
2. Build or prepare:
3. Run checks / perform actions:
4. Expected results:

## Validation actually performed

List exact checks, tested commit/environment, and outcomes. Include host/guest evidence
when the task claims live behavior. Identify checks that could not run and how that
affects acceptance.
Keep proposed reviewer steps above separate from checks already run. Source-only delivery
does not waive runtime evidence required by the task's acceptance criteria.

## Integration review

- [ ] Task acceptance criteria are met and the handoff is current.
- [ ] Dependencies are integrated; the final diff against `main` contains only this task.
- [ ] Confirmed product decisions are preserved; proposed defaults are clearly identified.
- [ ] Relevant regression and integration checks passed, with limitations stated above.
- [ ] Reproducible test steps and expected results are included; source-only/documentation-only delivery is explicit.
- [ ] No unresolved WIP, placeholder product flow, game assets, or local build artifacts.

Keep this PR in draft until these requirements are met. `done` is recorded after the
actual merge, not when a branch is published or review is requested.
