# COOP 003 Campaign preparation UI design

## Coordination

- Status: active
- Owner/session: Codex COOP-003 / 2026-11-18 user assignment
- Canonical branch: `docs/coop-003-preparation-ui`
- Base commit: `7e033adde95881baad06c6877cbbd6eef8197d96`
- Dependencies: none
- Last checkpoint: this initial claim commit (`HEAD`)
- Checkpoint location: `origin/docs/coop-003-preparation-ui`; worktree `.worktrees/COOP-003`
- Review/integration: claim recorded in coordination commit `7f54ee71`; COOP-001 coordinator notified of concurrent kickoff and acknowledged as sole ledger writer
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

Produce an implementable campaign-style preparation layout and resource feasibility study.
Each player equips their soldier; the host can also equip unclaimed AI soldiers. Investigate
the original singleplayer presentation/components before proposing replacement UI technology.

## Files and shared contracts

Own `docs/ui/campaign_preparation.md` and original mockups/resource inventories. Inspect
`equipment_screen.*`, `equipment_panel.*`, `equipment_frames.*`, `menu_soldier.*`, screen
descriptions, controls, and localization. Coordinate UI-facing model needs with COOP-002.
Do not change shipping menu behavior or commit extracted game assets.

## Acceptance criteria

- Show briefing, four squad slots, human/AI ownership, local equipment editor, stock,
  mission essentials, and a proposed ready/deploy presentation in the campaign style.
- Cover host/guest/AI editing rights, read-only viewing, disconnect/claim changes, scarce
  items, rejected edits, and missions with restricted squads.
- Identify reusable campaign components, required new controls/assets, localization, and
  practical rendering/input constraints. Explain how the new UI avoids global-state edits.
- Provide a buildable implementation route and accessible preview/mockup if feasible;
  record unavailable game resources without pretending the screen has run in-game.
- List genuinely blocking product questions. Multiplayer loadout UI remains a fallback to
  discuss only if a concrete constraint requires it; do not silently adopt it.

## Handoff

- Completed: product direction and candidate components identified in the design.
- Remaining: layout, resource audit, state/interaction specification, consumer contract review.
- Next action: inspect campaign equipment construction and resources; publish layout, resource audit, and original offline HTML preview. Coordinate immutable snapshot/command needs with COOP-002 (thread `01a1ebd2-902d-7e32-9876-9818a845d361`).
- Blockers: none for code/layout investigation; game resources may limit visual verification.
- Decisions: campaign-style UI and per-player loadout ownership are confirmed.
- Validation: none for this task.
