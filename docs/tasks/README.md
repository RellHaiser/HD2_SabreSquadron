# Cooperative campaign task queue

Work is divided by deliverable, not by agent. Read the [workflow](../agent_workflow.md)
before claiming a task. Each linked record defines scope, dependencies, acceptance,
and a handoff. The status in the record on the active task branch is authoritative
for implementation progress; `main` only knows work that has been integrated.

The [coordination ledger template](COORDINATION.md) becomes the live claim/review map on
`coord/coop-work-queue`. Read that branch's ledger to find the current coordinator and
accepted assignments. This operational branch keeps in-progress coordination out of `main`.

## Initial tasks

The first three tasks are independently ready for a claim after this setup is integrated.
The remaining tasks begin as planned. A dependency means its required result must have
merged into `main` before implementation starts; an open branch alone is insufficient.

| Task | Deliverable | Dependencies | Canonical branch |
| --- | --- | --- | --- |
| [COOP-001](COOP-001.md) | Reproducible build and baseline verification | None | `feature/coop-001-build-baseline` |
| [COOP-002](COOP-002.md) | Shared session, ownership, preparation, and protocol contracts | None | `feature/coop-002-contracts` |
| [COOP-003](COOP-003.md) | Campaign preparation UI feasibility and layout | None | `docs/coop-003-preparation-ui` |
| [COOP-004](COOP-004.md) | Soldier ownership and session model | COOP-002 | `feature/coop-004-ownership` |
| [COOP-005](COOP-005.md) | Loadout permissions and stock transactions | COOP-002 | `feature/coop-005-preparation-model` |
| [COOP-006](COOP-006.md) | Protocol codecs and compatibility negotiation | COOP-002 | `feature/coop-006-protocol` |
| [COOP-007](COOP-007.md) | Campaign preparation screen component | COOP-002, COOP-003 | `feature/coop-007-preparation-screen` |
| [COOP-008](COOP-008.md) | Ownership-aware controller and input adapters | COOP-001, COOP-004, COOP-006 | `feature/coop-008-controller-adapters` |
| [COOP-009](COOP-009.md) | Campaign mission lifecycle adapters | COOP-001, COOP-004, COOP-005, COOP-006 | `feature/coop-009-campaign-adapters` |
| [COOP-010](COOP-010.md) | Two players and two AI in one original mission | COOP-008, COOP-009 | `feature/coop-010-two-player-mission` |

COOP-004 through COOP-007 can proceed in parallel once their respective contracts and
UI investigation are integrated. COOP-008 and COOP-009 share some legacy integration
files and need a coordinator-agreed edit sequence. COOP-010 is the first playable
integration gate. The preparation screen is a separate component until it is connected
to the complete campaign/menu flow in a subsequent task.

With one agent, complete ready tasks in dependency order. With more agents, claim
independent tasks or split a larger task into smaller complete deliverables. There is
no required team size and no permanent UI, networking, or integration agent.

## Claiming and resuming work

Once assigned, the agent has full control of its task branch, including editing,
committing, rebasing, resolving conflicts, pushing to the project remote, and creating
or updating its PR without repeated user approval. This authority covers the task branch;
`main` still accepts only finished, reviewed work.

1. Check the current coordinator's claim map, local worktrees, and existing local/remote
   task branches or PRs. Do not infer availability from the planning copy on `main`.
2. Get a claim or reassignment acknowledged. Use the canonical branch above; if it already
   exists, inspect it and follow the takeover procedure instead of inventing a new branch.
3. Update that branch's task record with owner, status, base SHA, scope, and next action.
4. Before stopping, commit a handoff and preserve the code checkpoint. Record check results
   and where the checkpoint is accessible, including whether it is local only.
5. When the task implementation and required validation are complete, push the branch and
   create/update a PR targeting `main`. Include reproducible **How to test** steps with
   prerequisites, commands/actions, and expected results. State **Source-only** explicitly
   when no runnable artifact is supplied, or **Documentation-only** for documentation tasks.
   Separate checks actually run from suggested reviewer checks and record the PR URL in
   the task handoff. Task status remains `review` until integration, then becomes `done`.

The coordinator transfers the active claim map and merge queue in a durable handoff when
that role changes. A branch file is a progress record; branch discovery and coordinator
acknowledgment together prevent two clones from claiming the same task independently.

## Adding and changing tasks

Copy [TEMPLATE.md](TEMPLATE.md), assign a new unused COOP ID, and choose a bounded result
that can meet its own acceptance criteria. List dependencies and expected files so another
agent can assess whether it can run in parallel. The coordinator maintains this index;
individual workers update their task files rather than rewriting the shared queue.

COOP-002 should refine or split the downstream adapter tasks after the coupling audit.
Preserve IDs for existing results and record replacements or follow-up IDs explicitly.
Resolve genuinely blocking product decisions before their dependent work becomes ready.

Later tasks will cover the production menu entry and session setup, live preparation
synchronization, campaign transitions and debrief, save/load and reconnect, joining a
running mission, and full mission verification. Create their bounded records as the
contracts and first playable mission expose the actual work; the full goal remains in
the [campaign roadmap](../base_game_coop.md#implementation-milestones).
