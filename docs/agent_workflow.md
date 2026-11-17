# Cooperative campaign agent workflow

`main` contains finished, integrated work. All implementation, experiments, and incomplete
integration belong on task branches. A task belongs to a branch and a written handoff, so
another agent can continue it without access to the original conversation.

Read [AGENTS.md](../AGENTS.md), the [campaign design](base_game_coop.md), and the
[task index](tasks/README.md) before choosing work. The number of agents can change at any
time; tasks have dependencies and acceptance criteria rather than permanent agent assignments.

## Branches and worktrees

Once assigned, an agent has full control of its task branch, including editing, committing,
checkpointing, rebasing, resolving conflicts, pushing to the configured project remote,
and creating or updating its pull request. These task-branch actions are authorized by
the project owner and do not require repeated user approval. Coordinate changes that
affect another worker's shared history or contracts. Integration into `main` remains
subject to the review and acceptance gates below.

Use one branch per bounded task and one separate worktree per active worker. Name branches
`feature/coop-001-build-baseline`, `fix/coop-012-controller-handoff`, or
`docs/coop-003-preparation-ui` using the task's ID and a short description. Avoid branch
names tied to an agent or to an entire subsystem's unfinished development.

Keep the root checkout on `main` when it is serving as the integration reference. Worktrees
under `.worktrees/` are local working directories and must remain ignored by Git. Independent
task workers must not share a writable worktree. Helpers within one parent-owned task may
edit explicitly separate file scopes in that task's tree; the parent controls Git operations
and waits for those writers before switching branches or making an integration checkpoint.
Repository preparation itself uses a documentation branch rather than editing `main`.

For a new task, first get its claim accepted by the current coordinator, then inspect the
local repository. These commands illustrate COOP-001; substitute the actual task and branch:

```bash
git status --short
git worktree list
git branch --list 'feature/coop-001-*'
git branch --remotes --list '*/feature/coop-001-*'
git worktree add -b feature/coop-001-build-baseline .worktrees/COOP-001 main
cd .worktrees/COOP-001
git rev-parse HEAD
```

Record that base commit in the task file. If an existing branch or worktree already owns
the task, follow the takeover process instead of creating competing work. If a remote is
configured, refresh its refs using the project's authorized fetch process before claiming
work. A local branch can be ahead of the remote, so inspect both before resuming it.

Use the exact canonical branch recorded in the task file when claiming or resuming work.
Within one clone, creating that same branch twice fails; this protects against duplicate
branch creation, but does not reserve the task across clones or prevent a differently
named duplicate. Cross-clone claims require the coordinator's explicit acknowledgment in
the shared coordination channel before either worker starts. An unclaimed-looking task
file on `main` is not evidence that no one is working on it.

Do not switch the shared root checkout underneath another worker, move its files, reset
its branch, or discard its changes. A checkout with unexplained changes needs inspection,
not an automatic cleanup. Keep game data, build outputs, and local worktrees out of commits.

### Isolate build outputs too

Separate worktrees do not isolate the existing build tools' temporary directories.
`_re/tools/build_game.py` writes to fixed names under Python's temporary directory:
`hd2_game`, `hd2_game_checked`, `hd2_game_cov`, or `hd2_game_cov_checked`.
`_re/tools/build_check.py` uses `hd2_build_check` for its MSVC build. Concurrent builds of
the same variant can overwrite each other's objects and response files, or reuse objects
from a different branch.

Until the baseline/build task provides explicit unique output directories, either serialize
these legacy builds or launch each build with process-local Windows `TEMP` and `TMP`
pointing to a unique absolute directory for its worktree. Create that directory first and
verify Python's choice with `python -c "import tempfile; print(tempfile.gettempdir())"`
in the same environment. A directory under that worktree's ignored `build/` directory is
suitable. Run only one build at a time in a particular output directory. Review subsystem
harnesses for their own fixed paths before assuming they can run concurrently.

Set the environment only for the build process and its children. Do not change another
agent's shell, global environment settings, `HOME`, or `CODEX_HOME`. Record the unique
output path and relevant build variant in the task's validation evidence.

## Task records and coordination

Each task has a durable record at `docs/tasks/COOP-xxx.md`, based on
[the task template](tasks/TEMPLATE.md). The index describes the dependency graph and where
to start. The task file on its active branch describes current work; the copy on `main`
cannot show progress that has not been merged yet.

Use these states:

| State | Meaning |
| --- | --- |
| `planned` | Scope exists, but dependencies or decisions still need resolution. |
| `ready` | Dependencies are merged, the acceptance criteria are clear, and it can be claimed. |
| `active` | A named worker owns the task and is implementing it. |
| `blocked` | A specific dependency or missing decision prevents further progress on its scope. |
| `review` | The feature and its evidence are ready for review and integration. |
| `done` | The feature has merged into `main`; its merge commit is recorded. |

One coordinator serializes task claims and integration. This is a temporary responsibility,
not a permanent person or agent: with one agent, that agent does both jobs; with several,
one coordinates while others own tasks. On a coordinator change, transfer the active task
claims, pending reviews, and merge order before issuing new claims.

### Find the coordinator

The durable coordination ledger is `docs/tasks/COORDINATION.md` on the operational branch
`coord/coop-work-queue`. The copy on `main` is its initial template, not a live task lock.
Inspect that branch before choosing work:

```bash
git branch --list coord/coop-work-queue
git branch --remotes --list '*/coord/coop-work-queue'
git show coord/coop-work-queue:docs/tasks/COORDINATION.md
```

If only a remote branch exists, read the ledger using that actual remote ref after an
authorized fetch. The coordinator records its identity/contact, each accepted claim,
task branch, checkpoint, and pending review in this ledger. Claim acknowledgment points
to the ledger update; a worker does not need permission from the user for each task.

At kickoff, designate one available agent as coordinator. If no coordination branch exists,
that agent creates it from integrated `main` in its own `.worktrees/coord` worktree, updates
the ledger, and commits the assignment. A solo worker can take that role itself after
checking existing local/remote claims. Across clones, publish the assignment through the
authorized shared repository and resolve any competing coordinator before issuing claims.
Use ordinary pushes; a rejected competing update must be reconciled, not forced through.

Only the current coordinator writes that ledger. Its branch can contain operational
checkpoints and is never merged wholesale into `main`. Finished task records and dependency
changes reach `main` through reviewed task or documentation changes. Coordinator takeover
updates the same ledger after confirming the previous writer has stopped. Without remote
publication, this ledger coordinates only agents sharing its local repository.

Before accepting a claim, the coordinator checks the task's existing branch/PR and handoff,
dependency merges, and declared file scope. The worker then records its owner/session,
branch, base SHA, scope, and next checkpoint in its task file. Publish the claim before
substantial implementation. A task has one active writer; additional agents can review or
take separately scoped tasks.

An agent may request any ready task. More agents can take independent tasks or split a
large task into reviewed smaller ones. With fewer agents, finish tasks in dependency order.
Do not create a fixed assignment such as "agent 2 always owns UI."

Avoid having every worker edit the shared index. The coordinator maintains dependency
changes and integrates finished task updates. To inspect a task without checking it out:

```bash
git show feature/coop-001-build-baseline:docs/tasks/COOP-001.md
```

## Interfaces and overlapping work

Agree on the shared data/API contract before independent implementations depend on it.
Prefer a small, tested contract task that can merge independently, then create dependent
branches from the updated `main`. Record identity types, ownership rules, revisions,
failure cases, and protocol compatibility in the relevant task and design notes.

Declare expected files and interfaces in each task. File scope is a coordination aid,
not permission to silently change another task's contract. Notify the coordinator and
affected workers before broadening it. Shared files such as `game_app.cpp`, `profile.h`,
and the script VM are integration points and often need a serialized adapter task.

UI exploration can run alongside ownership and networking work using explicit mocks.
An approved mock or design is a finished design deliverable; a mock wired into a shipping
menu is not a finished gameplay feature. Keep design tasks separate from implementation
tasks so their acceptance criteria remain honest.

Do not merge unfinished dependency branches into a task just to unblock it. A stacked
branch can contain commits from other tasks, so merging it may land unrelated WIP.
If a stack is necessary, record the dependency branches and merge order, keep it in draft,
and review its final diff against `main` after those dependencies land. Never count a
dependent feature as ready merely because its dependency exists on an unmerged branch.

## Checkpoints and handoffs

Update the task record whenever scope changes and before stopping, becoming blocked, or
asking for review. Include:

- The current branch, base SHA, latest checkpoint commit, owner, and status.
- What works, what remains incomplete, and files/interfaces changed.
- Exact build/test commands and their outcomes, including checks that could not run.
- Any new design decision, its source, and unresolved questions. Keep proposed defaults
  distinct from user decisions.
- The next concrete action and any local prerequisites or artifacts needed to resume.
- Uncommitted changes and where they live, if an interruption prevented committing them.

Checkpoint commits may contain WIP on a task branch. Give them a clear message and keep
them out of `main`. When the project remote is available, push checkpoint commits
and use a draft PR so another machine can find the work. The PR must identify its task,
dependencies, scope, and handoff file. Branch publication and task PR creation are already
authorized; do not ask the user to approve them again. A local-only branch can be resumed only where its
Git objects and working files are available; a chat summary alone does not transfer them.

Do not require a remote service to work locally. Without one, the coordinator tracks
branch names and checkpoint SHAs in the local coordination ledger, and the task file carries the
handoff. If a task must move to another machine, arrange an authorized repository transfer
before releasing the original workspace.

### Taking over a task

1. Coordinate with its current owner, or with the coordinator if that owner is unavailable.
   Record the reassignment before editing. A quiet chat or elapsed time does not prove
   the original worker has stopped.
2. Read its task record from the task branch, the diff against its base and `main`, recent
   commits, open review notes, and the latest design decisions.
3. Check `git worktree list` and preserve any uncommitted files. Reuse an abandoned worktree
   only after its former writer has stopped; otherwise arrange a checkpoint and use a
   different branch/worktree. Never force a branch into a second active worktree.
4. Reproduce the last meaningful check, then update the owner, checkpoint, and next action.
   Continue the same task branch where practical. Record a replacement branch explicitly
   if recovery requires one.

An ordinary takeover of a locally available branch that is not checked out elsewhere:

```bash
git worktree list
git worktree add .worktrees/COOP-001 feature/coop-001-build-baseline
cd .worktrees/COOP-001
git status --short
git log -5 --oneline
git diff main...HEAD --stat
```

An available remote checkpoint can first be made into a local tracking branch using its
actual remote and branch names. Inspect existing local work before doing so. Avoid force
pushes during handoff, and do not rewrite commits another active task depends on.

## Review and integration

When the task implementation and required validation are complete, push the final branch
and create a pull request targeting `main`, or update its existing draft and mark it ready
for review. Record the PR URL in the task record. A local commit or final chat reply alone
does not complete the review handoff. If remote access is unavailable, preserve the local
checkpoint, record the publication blocker and prepared PR description, and report that
the PR still needs to be created.

Every completion PR description must include:

- The completed result, task/dependencies, and whether it is integrated gameplay, a
  component, source-only delivery, or documentation-only delivery. State **Source-only**
  explicitly when no runnable artifact is supplied; describe whether the code is wired
  into the game and whether a build or runtime test was performed.
- Reproducible **How to test** steps: required toolchain, game data/version, setup/build
  and launch commands, relevant mission/settings, player count and host/guest actions
  where applicable, and the expected result after each meaningful step.
- Checks actually run, their environment and outcomes, and any checks not run. Separate
  suggested reviewer steps from validation already performed. For source-only components,
  explain how to build/run the relevant harness or tests; for documentation-only tasks,
  give document review and link/diff checks instead of invented gameplay steps.

Use [the PR template](../.github/pull_request_template.md). Source-only delivery describes
what is supplied; it does not waive runtime evidence required by the task's acceptance
criteria. Keep a PR in draft if those criteria remain unmet.

The unit of integration is a finished, reviewable task with a clear result. A reusable
ownership module with meaningful tests can be finished before the entire co-op mode is
playable. Its task must describe that limited outcome and it must not expose unfinished
user-facing behavior. Do not label a shell implementation or unresolved TODO a complete
feature merely because it builds.

Before setting `review`, the worker verifies the acceptance criteria, updates the handoff,
and checks the complete diff. For example, on the task branch:

```bash
git status --short
git diff --check
git diff main...HEAD --stat
git log --oneline main..HEAD
```

Run the checks appropriate to the task. For legacy game changes this generally includes
the relevant compile/build checks and subsystem harnesses; controller, replication, and
mission acceptance also require host/guest runtime evidence. A syntax check is not a
playable build, and loading a mission is not evidence that it completes. Record unavailable
toolchains, game data, or test machines explicitly. Scope an independently mergeable
foundation task honestly rather than waiving an unmet playable-feature acceptance gate.

The integrator reviews one candidate at a time against current `main`, including its
dependencies, task record, code, resource changes, and test evidence. A different agent
reviews when available; with one agent, perform a separate review pass. The integrator
can change without changing task branches.

Prepare and validate the combined result on a separate integration worktree/branch when
needed. Resolve conflicts there or on the feature branch and repeat the affected checks.
Do not use stable `main` as a scratch space for conflict resolution. Review the final
commit list so no draft dependency or unrelated change lands accidentally.

Merge the reviewed feature through the repository's authorized integration mechanism.
Keep feature history identifiable with its task ID, whether using a PR squash or a merge
commit. Task-branch pushes and PR creation are authorized as above. Merging into `main`
requires the integration gates; changing repository branch protection is a separate action.

After a successful merge, record `done` and the actual merged commit in the task record
through a finished documentation follow-up if the integration mechanism could not include
that metadata. Then update dependent tasks that are now ready. A review request, successful
local merge rehearsal, or published branch does not make a task `done`.

Keep branches and worktrees until their useful checkpoints are preserved and all writers
have stopped. Cleanup is separate from accepting a feature; never automatically delete
another agent's branch or worktree.

## Changes to the project vision

New user decisions update the campaign design and affected task acceptance criteria.
Record which tasks need adjustment and notify their owners. Completed work remains
traceable; make follow-up tasks rather than silently changing the meaning of an old result.
Do not let a convenient reuse of native multiplayer infrastructure override the requested
singleplayer experience or introduce the native multiplayer UI without a design decision.
