# Working on the cooperative campaign

## Start here

Read [the design](docs/base_game_coop.md), [the agent workflow](docs/agent_workflow.md),
and [the task index](docs/tasks/README.md). For an existing task, read its record from
its active branch, its commits and diff, and its latest handoff before editing.
Find the coordinator and live claims in `docs/tasks/COORDINATION.md` on the
`coord/coop-work-queue` branch; see the workflow for initial kickoff if it does not exist.
User instructions and later confirmed design decisions take precedence over these notes.

## Branches and ownership

- `main` is the stable branch. Do not implement or checkpoint WIP there. Merge only
  finished, reviewed tasks whose stated acceptance criteria have been met.
- Work belongs to a task, not to a particular agent. Use the task's canonical branch,
  an isolated worktree for each independent worker, and a durable task record. Get the
  coordinator's claim acknowledgment before substantial work; take over an existing
  branch instead of starting a duplicate implementation.
- Once assigned, the agent has full control of its task branch: edit, commit, checkpoint,
  rebase, resolve conflicts, push to the project remote, and create/update its pull request
  without repeated user approval. Preserve other workers' branches and shared checkpoints;
  merging into `main` still follows the integration review gates.
- The coordinator and integrator are transferable responsibilities. One agent may do
  both when working alone. More agents take independent ready tasks; fewer agents
  follow the dependencies. No permanent agent-to-subsystem assignments are required.
  Operational ledger checkpoints stay on `coord/coop-work-queue`, outside `main`.
- Preserve another worker's changes, branches, and worktrees. Do not change the shared
  checkout's branch underneath a worker or automatically reset, clean, or delete work.
- When delegating within a single task, the parent owns Git operations. Give helpers
  separate file scopes; independent implementation tasks need separate worktrees.
  Read-only review helpers may inspect the parent's tree without changing it.
- Update the task handoff before stopping, blocking, transferring work, or requesting
  review. Record a checkpoint commit, exact checks and results, remaining work, and the
  next concrete action. Push checkpoints so another agent can resume the branch.
- When implementation and required validation are complete, push the branch and create
  or update a pull request targeting `main`. Its description must give reproducible test
  steps with prerequisites and expected results, distinguish checks actually run from
  suggested checks, and explicitly state when delivery is source-only or documentation-only.
  Source-only delivery does not waive a task's required runtime acceptance evidence.

## Product boundaries

- Preserve the base singleplayer campaign experience: its soldiers, missions, scripts,
  objectives, equipment rules, and progression. Up to four players control the squad;
  AI controls unclaimed soldiers.
- Each human chooses their own loadout. The host edits their own soldier and unclaimed
  AI soldiers, not another connected player's soldier. Network authority validates
  choices; it does not transfer the choice to the host.
- Build a campaign-style preparation UI. Reuse networking/infrastructure and suitable
  singleplayer components; do not default to multiplayer loadout screens, scoreboards,
  ranks, rounds, or respawns. Discuss a concrete UI fallback before adopting it.
- Keep confirmed product decisions separate from proposed defaults. Record new user
  decisions in the design and adjust affected open tasks. Ask only when a decision
  actually blocks dependent work; continue independent work in the meantime.

## Implementation and validation

- Preserve `IsLocalGame`, server/client authority, and legacy behavior. Add explicit
  campaign-session and ownership concepts rather than globally redefining local play.
- Follow the hand port's `C_`/`S_`, PascalCase method, and `m_` member conventions. Original
  address comments describe original functions; do not invent addresses for new code.
  Treat serialized layouts, protocol fields, engine interfaces, and x86 assumptions as
  compatibility boundaries and audit their consumers when changing them.
- Keep new code out of `src/tier1`; it is generated reference code, not the runnable
  implementation. Do not commit installed game data, original binaries, build output,
  credentials, or local worktrees.
- Agree on shared contracts before parallel consumers implement them. Declare file
  scope, coordinate changes to shared integration files, and avoid unrelated rewrites.
- Run checks appropriate to the change. A syntax check does not establish a runnable
  build; a runnable build does not establish host/guest behavior. Record limitations
  honestly, and do not mark a playable feature complete without its runtime evidence.
- Legacy builds share fixed temporary output paths. Isolate process-local build outputs
  or serialize those builds as described in the workflow; worktrees alone are insufficient.

Task records on `main` are planning/merge records, not a live lock service. Inspect the
active branch and current coordination state before treating a task as available.
