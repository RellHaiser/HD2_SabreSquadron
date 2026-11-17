# COOP 003 Campaign preparation UI design

## Coordination

- Status: done
- Owner/session: Codex COOP-003 / 2026-11-18 user assignment
- Canonical branch: `docs/coop-003-preparation-ui`
- Base commit: `7e033adde95881baad06c6877cbbd6eef8197d96`
- Dependencies: none
- Last checkpoint: implementation/design `1e998bd2`; this handoff commit is the latest metadata checkpoint (`HEAD`)
- Checkpoint location: `origin/docs/coop-003-preparation-ui`; worktree `.worktrees/COOP-003`
- Review/integration: coordinator review passed; integrated through PR #3; actual merge `dd2aa98e2571eef5bd8d542d51d43113bd223745`
- Pull request: https://github.com/xcntroller/HD2_BaseCoop/pull/3 (merged)

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

- Completed: campaign layout/interaction/state specification, source resource inventory,
  native implementation route, original accessible offline HTML fixture, and repeatable
  browser smoke checks. Shipping behavior and installed game data are untouched.
- Delivery: **Documentation-only**. HTML is a synthetic design preview, not the native
  COOP-007 component, a playable feature, or connected loadout selection. No new game
  source/header/resources, engine build or live host/guest test is supplied or claimed.
- Files changed: this task record and `docs/ui/campaign_preparation.md`,
  `campaign_preparation_resources.md`, `campaign_preparation.html`,
  `check_campaign_preparation.py`.
- Contract review: COOP-002 owner (thread `01a1ebd2-902d-7e32-9876-9818a845d361`)
  reviewed the consumer boundary and confirmed semantic compatibility; requested explicit
  PreparationRevision/ViewRevision and seven interface methods, now incorporated.
  COOP-003 reciprocally reviewed its draft with no blocking consumer findings.
  Contract checkpoint: `6d3b5443` on `feature/coop-002-contracts`.
- Independent full review: COOP-002 owner reviewed `941a9350`, inspected specification,
  resource inventory, HTML/driver and host/guest screenshots; spot-checked source claims
  and reproduced browser smoke (PASS all scenarios/three widths, zero page errors) plus
  `git diff main...HEAD --check`. No blocking findings. Low-priority spectator-table
  ambiguity corrected to preserve host-without-a-soldier AI editing rights. A subsequent
  one-row own-review clarification (`7fb4c680`) preserves edit/deploy validation semantics.
  COOP-011 will supply compiled declarations; UI fake implementation does not depend on
  COOP-005 model implementation. No unmerged contract branch was stacked here.
- Decisions: confirmed campaign style and per-human equipment ownership are preserved.
  Ready quorum, roster/claim policy, scarce-stock UX, and equipment-step availability
  remain explicitly proposed/open. No product decision blocks this design deliverable.
- Resource limits: local Steam archives/language files/briefing videos were found by
  read-only inventory; no loose Models or Tables directory. Model geometry/materials,
  font fit/glyphs, decoded archive members and native input/rendering remain unverified.
  This does not imply absent game data or require multiplayer UI fallback.
- Publication: branch and PR published; connector PR creation returned HTTP 403, so the
  authenticated repository GitHub CLI created PR #3 successfully.
- Remaining task work: separate review/integration, then record actual merge SHA through
  coordinator's finished follow-up. Native implementation/runtime evidence belongs to
  COOP-007 and subsequent integration tasks; this deliverable does not waive their gates.
- Next concrete action: coordinator reviews published PR against `main`; check the
  acceptance map and reproduce browser/document checks below. Keep task `review` until
  actual integration; do not mark it `done` merely because branch/PR exists.
- Uncommitted deliverables: none after this checkpoint. Generated screenshots remain
  in ignored `build/coop-003-preview/`; no extracted assets or binaries committed.

## Acceptance evidence

| Criterion | Evidence |
| --- | --- |
| Briefing, four slots, human/AI, local editor, stock, essentials, ready/deploy | `docs/ui/campaign_preparation.md` Layout; offline HTML fixture and generated screenshots. |
| Edit/view rights, disconnect/claim, scarcity/rejection, restricted squad | Rights and state tables; browser checks for visible permissions, atomic display, competing item and claim-before-edit races, one-soldier spectator state. |
| Campaign component reuse, new controls/assets, localization and constraints | `docs/ui/campaign_preparation_resources.md` source-symbol inventory; global-state hazard table; source-observed vs installed-data-unverified distinction. |
| Buildable route and accessible feasible preview | Native LS3D/C++17 route and adapter sequence specified; browser fixture runs directly without assets/dependencies; optional smoke driver included. Native code is a later task, so no new native build is claimed. |
| Blocking product questions, fallback boundary | Explicit proposed defaults/open gates in specification; no unresolved blocker for independent design review; no concrete multiplayer UI fallback constraint found. |

## Validation actually performed

Environment: Linux, Python 3.14, Playwright 1.65.2, existing Chromium 158.0.8127.18.
Browser fixture checked at design checkpoint `1e998bd2`; subsequent handoff-only edits
cannot change fixture behavior.

```bash
python docs/ui/check_campaign_preparation.py --browser /home/control/.cache/ms-playwright/chromium-1254/chrome-linux64/chrome --screenshots build/coop-003-preview
git diff --check
git diff main...HEAD --check
git diff main...HEAD --stat
git log --oneline main..HEAD
```

- Browser smoke: **PASS** permissions (host own/AI/other human, guest own/others), pending
  unchanged committed display, accepted equip/return conservation, scarce stock rejection,
  claim-before-edit rejection, disconnect retaining gear, explicit rejection, receiving
  state, readiness reset/selection persistence, host-only deploy/freeze, restricted guest
  spectator, Enter/Space, skills toggle, and leave. Zero JavaScript page errors.
- Viewports: **PASS** no horizontal overflow at 1280×800, 1024×768, and 390×844; footer
  reachable by scrolling. Browser layout is not proof of engine resolution behavior.
- Visual review: host, guest/read-only and 390 px layouts inspected from browser screenshots;
  clear status/ownership and no clipped content observed. Generated fixture images are local,
  reproducible output, not installed game art.
- Markdown relative link targets checked with Python: **PASS**, no missing targets.
- Diff/commit scope checks: **PASS**, only own task record and `docs/ui/`; no shipping code,
  game assets, local worktrees or build artifacts in task diff.
- Not run: game executable/build, native model loading, localization/glyph expansion,
  actual network/host/guest behavior. None is claimed by this design task; these remain
  required downstream checks, not optional substitutes for browser evidence.

## Reviewer reproduction

1. Open `docs/ui/campaign_preparation.html` in a browser. No game, build or network needed.
   Select host/guest and each slot; host edits own/AI only, guest edits own only.
2. Exercise fixture buttons for scarce stock, disconnect, claim during a pending AI edit,
   restricted mission, rejection, snapshot loading and remote readiness. Expected behavior
   is in the specification's review matrix. Reset between independent scenarios.
3. Optional automation: install `playwright` plus `python -m playwright install chromium`,
   then run `python docs/ui/check_campaign_preparation.py` (or pass an existing browser as
   in actual validation). Expected: PASS line, zero page errors.
4. Review source-symbol/resource claims against the unchanged hand port, check confirmed
   versus proposed policies, and run the diff commands above. Expected: documentation-only
   changes and a reusable UI consumer boundary without global squad/stock writes.

## Integration outcome

Actual merge: `dd2aa98e2571eef5bd8d542d51d43113bd223745`. The coordinator reviewed the final diff and evidence against `main`; the finished documentation deliverable is integrated. No task work remains. Earlier review/pending language above records the pre-merge handoff, not the current status.

Coordinator reproduced the browser smoke (all scenarios, three widths, zero page errors), inspected the host preview, checked source/resource claims and reviewed both final prose corrections at `b4030c11`. COOP-002 independently reviewed the full deliverable. Native component/runtime checks remain COOP-007 and later integration gates.
