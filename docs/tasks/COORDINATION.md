# Cooperative campaign coordination ledger

The live copy of this ledger belongs to `coord/coop-work-queue`. The copy on `main` is
the initial template. See the [workflow](../agent_workflow.md#find-the-coordinator) before
creating or updating the operational branch. Do not merge its WIP ledger into `main`.

## Coordinator

- Owner/session: unassigned; no implementation task has been claimed by this setup.
- Contact/shared channel: record where agents can request claims and receive acknowledgment.
- Integrator: unassigned; may be the same agent.
- Latest checkpoint: none; `HEAD` identifies the ledger update when committing this record.
- Publication: local only until an authorized shared remote checkpoint is recorded.
- Next action: designate the kickoff coordinator and publish its assignment before claims.

## Accepted task claims

| Task | Owner/session | Task branch | Status | Accessible checkpoint or PR | Next action |
| --- | --- | --- | --- | --- | --- |

Only the coordinator adds or reassigns claims. A missing row is not sufficient evidence
of availability: check worktrees and existing task branches as well. Task records carry
detailed progress; this ledger locates their active owners and checkpoints.

## Review and integration queue

No candidates yet. Record task, reviewed commit, dependency merges, reviewer/findings,
remaining validation, and proposed merge order. Integrate serially against current `main`.

## Coordinator handoff

- Completed: no feature coordination started.
- Active writers: none assigned by this ledger.
- Blocked tasks/decisions: see individual task records and the campaign design.
- Next coordinator action: inspect current branches/worktrees before assigning ready tasks.
- Transfer acknowledgment: record old/new owner and that the former ledger writer stopped.

Keep this record and task checkpoints accessible to successors. When moving between
machines, a local-only ledger or unpublished task branch must be transferred explicitly.
