# COOP 002 Shared campaign co-op contracts

## Coordination

- Status: review
- Owner/session: Codex COOP-002 / `01a1ebd2-902d-7e32-9876-9818a845d361` (2026-11-18)
- Canonical branch: `feature/coop-002-contracts`
- Base commit: `7e033adde95881baad06c6877cbbd6eef8197d96`
- Dependencies: none
- Last checkpoint: final contract/review commit; this branch `HEAD` (claim checkpoint `4e28d883`)
- Checkpoint location: `.worktrees/COOP-002`; `origin/feature/coop-002-contracts`
- Review/integration: document self-review and COOP-003 consumer review passed; coordinator integration review requested; not merged
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

Define the shared contracts that let ownership, preparation, networking, and UI develop
independently. Audit coupling in legacy code and choose narrow adapters. This completes
an interface/design foundation; it does not claim to enable campaign co-op.

## Files and shared contracts

Own `docs/coop_contracts.md`; this delivery introduces no shared headers. Compiled
declarations are the coordinator-approved follow-up COOP-011.
Inspect `profile.h`, `human_control.cpp`, `player_net.cpp`, `spawn_point.cpp`, `game_app.cpp`,
`game_rounds.cpp`, equipment code, and script player-team queries. Coordinate names and
types with COOP-003; refine COOP-004 through COOP-010 file scopes before marking them ready. The coordinator
authorized the narrow task-index dependency update and new `docs/tasks/COOP-011.md`.

## Acceptance criteria

- Define session/mission/preparation identity, soldier identity, player ownership, revision
  and generation semantics, permitted transitions, requests/results, and snapshot boundaries.
- Specify equipment permissions: human own soldier; host own and unclaimed AI soldiers.
  Define a UI-facing preparation interface, so the UI can use a fake implementation.
- Define protocol compatibility negotiation and rejection semantics without assuming wire
  layouts or legacy object IDs can change freely.
- Map actor-ID/player-slot assumptions and local/network rule branches to adapter tasks.
- Specify invariants and behavioral tests for downstream modules. Compile any new headers
  in their intended target environment; document-level contracts have an explicit review.
- Keep unconfirmed product decisions visible and isolate them from independent interfaces.

## Validation evidence and document review

Documentation-only delivery on Fedora/Linux with Python 3 and Git. No source/header,
game asset or build output was changed. A target-header compile is inapplicable to this
delivery; COOP-011 explicitly requires MSVC x86 compilation before compiled consumers
can proceed. No game build, runtime session or gameplay validation is claimed.

Checks run against the final contract/task changes before publication:

- `git diff --check main`: passed, no whitespace errors.
- Changed-document local link/heading check below: passed for 11 changed Markdown files;
  repeated for the final tree. The script reports the link count.
- Task index/record canonical branch and dependency check below: passed for 11 tasks;
  no missing dependency and no cycle.
- Separate source/document review: verified every audit file and named symbol against
  base `7e033add`; corrected preliminary symbol names to their actual port spellings.
  Traced competing claims, owner changes during equipment edits, stock conflicts,
  ready-view updates, freeze races, replay/slot reuse and reconstruction barriers.
- COOP-003 consumer review (2026-11-18, thread `01a1ebd2-c9c0-73a1-9495-337780d904f6`):
  read the complete draft and reported sufficient UI coverage with no blocking findings.
  Its spec adopted the seven method names, preparation/view revision distinction,
  explicit unresolved-policy blocker, fresh generation on abort and result/resync behavior.
  COOP-002 reciprocally reviewed its UI boundary; the race scenario now distinguishes
  an edit committed before claim from one invalidated by claim. Catalog/localization
  descriptors remain immutable data rather than engine pointers.
- Coordinator review preparation: claim/scope acknowledgment `c5a2f841`; COOP-011 and
  the index dependency refinement were explicitly approved through the coordinator
  thread `01a1ebd1-3990-7553-920e-7cfee0148527`. Only that thread writes the live ledger.

Acceptance review:

| Criterion | Evidence in contract |
| --- | --- |
| Identity/revision/transition/request/snapshot definitions | Identity table, lifecycle table, ownership and snapshot sections. |
| Equipment rights and fake-ready UI interface | Preparation interface/snapshot schema, exact permission table and atomic transaction rules. |
| Compatibility and legacy layout preservation | Negotiation states, rejection matrix, unchanged native join/type layouts and explicit later attachment obligations. |
| Actor/player and rule coupling audit | Source-symbol audit with named downstream adapter owner and serialization sequence. |
| Behavioral evidence and explicit document review | Downstream test matrix; source/self-review and COOP-003 consumer review recorded here. No new headers require compilation in this task. |
| Unconfirmed decisions isolated | Open-policy table and injected-policy/PolicyRequired boundary; no production default silently confirmed. |

## PR test instructions and delivery

**Documentation-only.** Prerequisites: repository checkout, Git and Python 3. No MSVC,
game installation or engine DLL is needed for these document checks. Review the design,
contract, task scope refinements and COOP-011 together. Expected result: contracts meet
the acceptance review above; COOP-004 through COOP-007 remain planned behind the new
compiled-declaration gate; there is no shipping gameplay change.

Run the actual link/anchor check from this task checkout (before or after committing):

```bash
python - <<'PY'
from pathlib import Path
import re, subprocess
files = subprocess.check_output(['git', 'diff', '--name-only', 'main', '--', '*.md'], text=True).splitlines()
checked = 0
for name in files:
    p = Path(name)
    for target in re.findall(r'\]\(([^)\s]+)\)', p.read_text()):
        if '://' in target:
            continue
        rel, _, anchor = target.partition('#')
        dest = (p.parent / rel).resolve() if rel else p.resolve()
        assert dest.is_file(), (name, target)
        if anchor and dest.suffix == '.md':
            headings = re.findall(r'^#+\s+(.+)$', dest.read_text(), re.M)
            slugs = {re.sub(r'[^\w\- ]', '', h.lower()).replace(' ', '-') for h in headings}
            assert anchor in slugs, (name, target, 'missing anchor')
        checked += 1
print(f'PASS: {len(files)} changed Markdown files, {checked} local links/anchors')
PY
```

Check the canonical branches and dependency graph:

```bash
python - <<'PY'
from pathlib import Path
import re
index = Path('docs/tasks/README.md').read_text()
rows = re.findall(r'\| \[(COOP-\d+)\]\([^)]*\) \| [^|]+ \| ([^|]+) \| `([^`]+)` \|', index)
deps = {}
for task, dep, branch in rows:
    text = Path(f'docs/tasks/{task}.md').read_text()
    assert f'- Canonical branch: `{branch}`' in text, task
    indexed = set(re.findall(r'COOP-\d+', dep))
    recorded = set(re.findall(r'COOP-\d+', re.search(r'^- Dependencies: (.*)$', text, re.M)[1]))
    assert indexed == recorded, (task, indexed, recorded)
    deps[task] = indexed

def visit(task, stack):
    assert task not in stack, (task, 'dependency cycle')
    for dep in deps[task]:
        assert dep in deps, (task, dep)
        visit(dep, stack | {task})
for task in deps:
    visit(task, set())
print(f'PASS: {len(rows)} task branches/dependencies agree; graph is acyclic')
PY
```

Then run `git diff --check main`, inspect `git diff main...HEAD --stat` and
`git log --oneline main..HEAD`, and confirm no source or generated assets are in the
change. These commands were run locally; a reviewer should repeat them at the PR head.
After integration use the PR base/merge parents for comparison instead of updated main.

## Handoff

- Completed: documentation-level session/ownership/preparation/protocol contract, source
  coupling audit, behavioral matrix, COOP-003 consumer review and explicit acceptance
  review. Refined COOP-004..010 scopes; added coordinator-approved COOP-011 declaration
  task and synchronized task-index dependencies. Preserved all proposed product policies.
- Remaining: coordinator's final integration review and merge; no implementation work is
  outstanding within this documentation task. Compiled declarations/models/codecs/UI and
  runtime gameplay belong to their recorded downstream tasks.
- Next action: coordinator reviews the published PR against current `main`, checks the
  dependency/scope diff, then integrates through the normal review gate. Record actual
  merge metadata and mark COOP-011 ready after integration; do not mark model/UI tasks ready.
- Blockers: none for review. This task is not marked done before an actual merge.
- Decisions: confirmed behavior follows the campaign design. Aggregate preparation
  revisions, request deduplication and guard boundaries are engineering contracts;
  open selection/readiness/stock UX/persistence policies remain unconfirmed.
- Local state: task changes live only in `.worktrees/COOP-002` and its canonical branch;
  published checkpoint preserves the full handoff. Root `main` and other workers' files
  are untouched. No new public headers or game artifacts.
- Review notes: COOP-003 consumer review has no blocking findings. Keep COOP-011 as a
  separate future implementation claim; no duplicate DTO headers in downstream branches.
