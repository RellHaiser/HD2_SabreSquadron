# COOP 003 Campaign preparation UI design

## Coordination

- Status: ready
- Owner/session: unassigned
- Canonical branch: `docs/coop-003-preparation-ui`
- Base commit: not started
- Dependencies: none
- Last checkpoint: none
- Checkpoint location: none
- Review/integration: none

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
- Next action: inspect campaign equipment screen construction and list reusable controls.
- Blockers: none for code/layout investigation; game resources may limit visual verification.
- Decisions: campaign-style UI and per-player loadout ownership are confirmed.
- Validation: none for this task.
