# Campaign preparation design (COOP-003)

This is a **Documentation-only** design deliverable. The [offline mockup](campaign_preparation.html)
uses invented soldiers, mission text, quantities, and CSS artwork. It is not an engine
screen, a network implementation, or an approved product decision. No shipping code changes.
The [resource audit](campaign_preparation_resources.md) records what the source establishes
and what still needs inspection in the installed game. Design base: `7e033add`.

## Confirmed direction and proposed policies

The [campaign design](../base_game_coop.md) confirms a host-led original campaign, up to
four humans, AI in unclaimed soldiers, each human equipping their own soldier, and the host
equipping their own and AI soldiers. Preserve campaign equipment, progression, and mission
restrictions. Use campaign presentation. Host authority validates a player's choice; it
does not give the host editing access to that player's soldier.

The layout and policies below are proposals for review. Host squad selection followed by
guest claims, readiness from every deployed human, and first accepted scarce-item requests
are prototype defaults, not newly confirmed user decisions. COOP-002 owns identity/API names
and semantics; this document is a consumer specification, not an alternative wire contract.

## Layout

Use the campaign's restrained document/table presentation: muted olive surrounding panels,
warm paper, dark text, portrait selection border, campaign item images and soldier preview.
No scores, ranks, class unlocks, rounds, respawn controls, or native multiplayer loadout flow.
The preview's typography, colours, initials, and item rows communicate the arrangement;
exact game materials/fonts require in-engine verification.

Reference composition: a 1024 × 768, 4:3 safe area. Reserve 40 px outer margins, a 64 px
heading, a 156 px four-slot squad strip, a 380 px content region, a 52 px readiness/action
strip, and gaps. The content region has a 220 px briefing pane and two roughly 350 px panes
for the editor and stock. Measurements are design targets, not observed model bounds.
At lower resolutions use Equipment / Briefing pages in the same scene, keeping the squad
strip, essentials summary, ownership, errors, and ready/deploy status visible. The browser
mockup instead reflows vertically; that is an accessibility aid, not proof of engine reflow.

| Region | Contents and behavior |
| --- | --- |
| Heading | Campaign/operation, mission name, difficulty, preparation status. Host identity is secondary information. |
| Four squad slots | Portrait, soldier name, deployed/unavailable status, human owner or AI, local edit/view label, readiness. Keep all four positions visible when the mission permits fewer soldiers; unavailable positions explain why. Selection changes the local view only. |
| Briefing | Campaign mission description and available objectives, scrollable without hiding essentials. Replay briefing uses the original campaign video step through a later campaign adapter; never advance everyone's campaign cursor to replay locally. Do not reveal objectives hidden by campaign scripts. |
| Local equipment editor | Selected soldier name and owner, explicit “Editing your soldier”, “Editing AI soldier”, or “Read only — equipped by {player}”. One 3D soldier preview, campaign inventory places, belt/backpack pages, weight/capacity, item information, skills toggle. Read-only views retain inspect/scroll controls. |
| Shared military stock | Existing campaign item groups and counts. Selected item details, quantity, destination, Equip/Return actions. Counts are committed availability, not private reservations. Pending edits do not visually debit stock. |
| Mission essentials | Required items, carried/required summary, eligible deployed carrier(s), missing/blocking reasons. Host cannot silently fill another human's inventory to satisfy a requirement. Essentials remain visible when briefing is closed. |
| Footer | Per-human ready state, relevant squad validation, local Ready/Not ready, host Deploy, Back/Leave. Show a reason when actions are unavailable. No guest Deploy action. |

Use text as well as colour for AI, read-only, missing essentials, pending, and readiness.
Player names are bounded display strings; truncate with an inspectable full name, escape
markup, and never interpret them as format strings or localization keys. Selection should
survive updates by persistent soldier identity, not array position.

## Rights and interactions

| Viewer and target | Inspect | Edit equipment / preset / replenish | Claim or squad selection |
| --- | --- | --- | --- |
| Human, own soldier | Yes | Yes, while preparation is editable | Release/switch only through ownership commands if policy allows. |
| Host, unclaimed AI soldier | Yes | Yes, same transaction path as own soldier | Host proposes squad changes; guest claims require an eligible available soldier. |
| Host, another connected human | Yes | No, including auto-equip and essentials distribution | No ordinary takeover. Ownership changes use the shared ownership policy. |
| Guest, another human or AI soldier | Yes | No | May request an eligible AI soldier under the proposed claim policy; viewing is never a claim. |
| Spectator / no eligible soldier | Yes, subject to snapshot visibility | No | Explain mission restriction/full squad; offer eligible claims if any. No new soldier or extra life. |
| Any viewer, unavailable/dead/nondeployed soldier | Explain availability; equipment inspection only if supplied | No | Cannot bypass mission restrictions or death. |

Clicking an inventory place or stock row selects it; a second explicit action equips or
returns a quantity. Mouse drag/drop can be retained, but a drag holds only item identity,
quantity, target, and the captured revision. Nothing is removed from committed state at
drag start. On drop submit one atomic transaction; replacing gear includes the stock return
and stock debit together. Esc cancels the gesture without any stock operation. Clicking a
soldier portrait cancels the gesture and changes the local editor target, never ownership.

Recommended selection/replenishment operates on the **selected editable soldier**. A future
bulk control must enumerate only currently permitted targets and revalidate all targets at
commit. Do not call the old squad-wide auto-equip or distribute-required handlers. If a
needed item is carried by another human, show its carrier and ask players to coordinate
outside this initial design; no transfer/reservation UI is promised.

Keyboard route: squad slots → briefing/page controls → inventory places → stock filters and
rows → quantity/destination → Equip/Return → Ready → Deploy/Leave. Tab/Shift+Tab, visible
focus, Enter/Space activation, arrow navigation within lists, and Esc cancellation are
requirements for COOP-007, not claims about the existing engine. Native focused activation
exists, but complete focus traversal needs implementation. Item descriptions must also be
available by selection/focus, not only hover. On rejection retain focus on the initiating
action; on ownership loss move focus from a removed edit control to the selected slot or
read-only heading. Avoid taking focus for routine remote stock updates.

## State and failure specification

All displayed equipment comes from the latest complete authoritative snapshot. A pending
gesture/command is local presentation, never shared committed state. Permit one outstanding
equipment command per client initially; keep inspect/scroll navigation available.

| Event/state | Required presentation and action |
| --- | --- |
| Initial snapshot loading | Retain heading and disabled slot outlines; show “Receiving preparation”. No equipment/claim/ready/deploy actions. Never render a previous mission's stock as current. |
| Accepted edit | Replace snapshot atomically; update selected loadout, stock, weight, essentials, and readiness together. Do not add an optimistic delta to an accepted snapshot. |
| Last item requested concurrently | Authority accepts at most the available quantity. Loser sees “Item no longer available” and current stock; their loadout is unchanged. No auto substitution or transfer. |
| Edit-time rule violation | Reject invalid placement/count and other edit-time violations without a partial edit; show the specific reason beside the action and in the status area. COOP-005 distinguishes these from permitted intermediate states (such as a missing essential or legacy-permitted overload) that block deployment. Preserve the existing Continue checks and authoritative unarmed confirmation. |
| Stale request | Display current snapshot and “Equipment changed; review and try again”. Do not automatically replay a new request with new revisions. |
| Ownership changes during a drag or pending edit | Cancel drag, disable mutation controls immediately, discard any unsent draft. A sent command is resolved by authority against captured ownership revision. Correlate late results but never apply an old-generation snapshot. |
| Guest disconnect | Same soldier/loadout remains, owner becomes AI when the authoritative update arrives; host may then edit it. No stock return or auto-equip on disconnect. Readiness is recomputed. |
| Claim succeeds | Preserve gear, update ownership revision, invalidate relevant readiness. Host's former AI editor becomes read-only. A failed claim reports current owner/availability; never assumes success locally. |
| Restricted squad | Host chooses only among allowed soldiers according to resolved policy; excess humans show “Spectating — mission permits {count} soldier(s)”. Hidden/disabled slots cannot be claimed. Recompute essentials against authoritative eligible deployment semantics. |
| Human not ready | Footer names who is still preparing. Ready references the committed preparation revision; it cannot be sent while equipment is pending. AI has a validation state, not a fictional human Ready button. |
| Relevant accepted edit/squad/ownership change | Authority invalidates readiness as required by the contract. Initial proposal: every committed preparation change clears deployed human readiness. Browsing, skill toggle, and local selection do not. |
| Deploy requested | Host requests deployment with expected committed revision. Server checks permissions, readiness policy, mission essentials and equipment validation, and pending accepted transactions; then freezes preparation and emits one committed deployment state. Disable mutation/claim/ready controls during freeze. |
| Deployment rejected/cancelled | A rejected proposal keeps the current authoritative phase. Aborting Frozen/Loading requires authority to open a fresh preparation generation if the campaign step permits it. Re-enable only after that snapshot arrives; no client timer returns to editing. |
| Host lost/session ends | Stop requests and show session-ended state. No local campaign stock/progress writeback. Rejoining starts from a fresh generation/snapshot. |
| Back/Leave | Local leave confirmation; host end-session confirmation distinguished from guest leave. Cancelling preserves snapshot and readiness. Earlier campaign-step navigation is host-controlled and invalidates preparation when committed. |

Readiness is not a loading barrier. Deployment starts a later mission-loading transaction;
this screen cannot start simulation merely because it displayed “Ready”. A spectator's
readiness inclusion and host cancellation policy remain explicit product choices.

## UI model boundary (COOP-002 consumer review)

The COOP-002 logical interface is `I_CampaignPreparation` with `GetSnapshot`, `SubmitEdit`,
`SubmitOwnership`, `SetReady`, `RequestDeploy`, `RequestResync`, and `PollEvent`.
COOP-002 defines identity types and the contract in `docs/coop_contracts.md` on
`feature/coop-002-contracts` (not yet merged at design time). No header, wire layout, integer width,
or serialized legacy structure is introduced here. The offline mockup is only a design
fixture and does not implement this API.

| Snapshot data needed | Why |
| --- | --- |
| Session/preparation generation, PreparationRevision, ViewRevision, OwnershipRevision and per-soldier ControlRevision; lifecycle/synchronization phase | Prevent old results or old ownership from enabling edits; bind Ready/Deploy to PreparationRevision. ViewRevision alone changes for readiness/status updates and does not invalidate others' readiness. |
| Local participant and host identities; capabilities and reason codes | Render rights from authority; no `IsLocalGame()` shortcut and no “host can edit all” inference. |
| Stable campaign soldier identity per visible slot; eligibility/deployed flag/restriction reason; human owner/AI | Selection survives reorder/claim; no actor ID, player slot, array index, or process pointer used as durable soldier identity. |
| Soldier display metadata, profile skills, preview descriptor, immutable loadout with stable item/place identity, quantities, weight/capacity | Render without dereferencing or editing `g_gameProfile.m_missionSquad`. |
| Stock entries with item metadata key, available count, allowed destinations, authoritative constraints | Show scarce shared resources without `C_StoreList::SetStock` binding to mutable profile stock. |
| Campaign mission/operation/difficulty, briefing text/image references, restriction summary, essentials and validation results | Preserve campaign information and rules, including deployed carriers and special required-item semantics. |
| Participants/eligible claim targets, ready acknowledgments for committed revision, deployment blockers | Explain readiness and restrictions, including spectators. Unresolved policy is an explicit PolicyRequired blocker in production; fixtures declare their proposed policy. |

Equipment requests carry generation, expected PreparationRevision and relevant ControlRevisions, target soldier,
request correlation ID, item/place/quantity and operation. The server derives editor identity
from the authenticated connection. Never trust a client-supplied “host” or editor ID as
authority. Snapshot capability flags guide display; every command still validates current
permission, stock, slots, rules, and phase at commit.

Results need correlation, accepted/duplicate/rejected/resync outcome, reason code with parameters,
and authoritative current state (or a specified resync mechanism). Treat duplicate success
as acknowledgement of an already-applied operation, not an additional edit. A revision-only
rejection must trigger resync before editing resumes. Apply snapshots monotonically within
the current generation; replacement generations come through session lifecycle handling,
not an unsolicited late response. Never clear a newer pending command on an unrelated result.

Ownership changes invalidate outstanding commands from the previous owner; no UI cancellation
can revoke an operation already committed. Equipment commit order determines whether an
edit preceded a claim. Always show the resulting authoritative inventory, even when the
old editor is now read-only. No disconnect path returns those items twice.

COOP-002 uses an aggregate PreparationRevision: two simultaneous editors may submit against
one revision, but once one edit commits the other gets StaleRevision even for different
soldiers. Explain the refresh without silently retrying. Production view application uses
ViewRevision to detect gaps and requests resync; no mixture of old stock and new equipment.
Detach event delivery on screen destruction; a pending edit may still commit, so reopening
reads fresh state instead of undoing local deltas. The host-without-a-soldier case still
permits AI editing while local Ready remains unavailable.

## Implementation route and integration boundaries

1. COOP-002 resolves the consumer contract above. Its coordinator-approved COOP-011 supplies
   shared identity/DTO/interface declarations at `src/game/campaign_contracts.h` before
   consumers bind to compiled types. COOP-005 adapts equipment validation and
   stock transactions, retaining mission-item and campaign rules; do not share mutable
   `S_SquadSoldier` or `C_ItemStock` objects with UI controls.
2. COOP-007 adds a new `C_` menu/presenter and `S_` view data in hand-port source. Build
   `S_ScreenDesc` groups against installed campaign model frames using the existing LS3D
   control factory. Frame construction may clone/reposition existing visuals with private
   meshes/materials; add original text/quad frames where the old scene has no anchor.
   Controls and preview renderer consume a local view copy and command sink. Do not invoke
   `C_EquipmentMenu::BuildScreens`, `BuildSquadList`, `SetupStore`, or `AutoEquip` as a new
   session bootstrap: they have global mutations. Do not change serialized class layouts
   to retrofit network identities into legacy profile structs.
3. Extract or wrap panel **presentation** deliberately. `BuildDescription`, item images,
   scroll controls, and `C_MenuSoldier` rendering are candidates; `SelectSoldier`, item moves,
   and `ReturnItems` require replacement bindings. Preview equipment uses copied descriptors
   and item definitions. A preview rotation/skills toggle cannot change shared gear.
4. Add ownership labels, four slot status rows, briefing pager, pending/error text, essentials
   validation list, Ready/Deploy controls and focus traversal in new UI-owned groups. Use
   a separate ID namespace; keep legacy panel child order/bitfields intact when reusing
   their handlers. Name-based lookup must fail with a useful resource error rather than
   unchecked null dereferences. New assets remain original or generated at install time.
5. COOP-007 first builds a fake-model engine harness outside normal menus. Exercise the
   matrix below; the fake consumes COOP-011 declarations independently of the COOP-005 model
   implementation. Bind live model adapters later. Production mode entry, session setup,
   briefing replay, and network mission launch are subsequent integration tasks. Preserve
   ordinary singleplayer and native multiplayer routing and `IsLocalGame` semantics.

Build feasibility rests on existing native controls/models, not a new browser runtime.
Use the repository's Windows x86 C++17/MSVC route in [README](../../README.md#building):
`python ls3df/build.py`, `python _re/tools/build_rw_data.py`, then
`python _re/tools/build_game.py`. Use an isolated process-local TEMP/TMP directory per
[workflow](../agent_workflow.md#isolate-build-outputs-too); COOP-001 owns baseline fixes.
That route has not been run for this documentation task and no new engine screen exists
to compile. Missing model anchors may require new native quad/text controls, but there is
no established constraint requiring the multiplayer UI fallback or a new UI technology.

## Review and verification

Open `docs/ui/campaign_preparation.html` directly in a browser; no server, internet, game
data, package installation, or build is required. Use the labeled **Design fixture controls**
to choose host/guest and scripted scenarios. Those controls are outside the proposed player
screen. Reset restores synthetic data. The mockup intentionally supports only a medkit
Equip/Return gesture; the specification describes full inventory, presets, and live flow.

| Check | Expected design result |
| --- | --- |
| Host selects each of four slots | Own and two AI slots editable; connected guest read-only. Selecting never claims. |
| Guest selects each slot | Own slot editable, host and AI read-only; eligible AI exposes separate Claim action. |
| Equip then wait for acknowledgement | Pending shown, stock/loadout unchanged until acceptance; then stock −1 and selected loadout +1; readiness reset. Return reverses both once. |
| Competing last item | Stock moves to another soldier once; local pending request rejects; no duplicate or partial edit. |
| Disconnect guest | Soldier retains loadout; AI ownership shown; host gains editing. |
| Claim commits before host's pending AI edit | Old-owner edit rejected, gear preserved, host read-only. If edit commits first, claim instead preserves that accepted gear. |
| Restricted mission | One eligible deployed slot; other positions unavailable with reason; guest spectator cannot edit/ready/deploy. |
| Rejected edit / receiving snapshot | Clear rejection / disabled mutations respectively; no invented accepted gear. |
| Ready and deploy | Remote-ready fixture + local Ready enables host Deploy only with essentials met; deploy freezes all edits. Guest never deploys. Changing selection preserves ready. |
| Keyboard, narrow view, long text | Named native controls, visible focus, readable status without colour; content/footer reachable by normal scrolling at 1024×768 and 1280×800, no horizontal overflow at 390 px browser width. Browser responsiveness does not establish engine resolution support. |

For optional automated fixture checks, install Python `playwright` and its Chromium browser
(`python -m pip install playwright`, `python -m playwright install chromium`), then run
`python docs/ui/check_campaign_preparation.py` from the repository root. An existing browser
can be supplied with `--browser /absolute/path/to/chrome`. Add
`--screenshots build/coop-003-preview` to capture original fixture states in ignored output.
This browser smoke check is not a game/model test. The mockup itself needs no packages.

Actual check evidence, environment, and review status live in [COOP-003](../tasks/COOP-003.md).
COOP-007 must additionally prove real item-place interactions, engine rendering/input,
localization expansion, original restricted missions (including base campaign missions 2
and 15), and relevant ordinary-play regressions. Live host/guest transaction evidence belongs
to later model/network/integration tasks; a browser fixture does not satisfy those gates.

## Blocking questions and remaining resource checks

No product question blocks this independent layout/resource deliverable. These decisions
block final live policy and should be confirmed before shipping the affected interaction:

- Who chooses the deployed roster and assigns/claims soldiers, and when may ownership change
  during preparation? Proposed: host chooses legal squad, humans claim available soldiers.
- Must every deployed human be ready; do spectators participate; may host override readiness;
  how does host cancel deploy and resume after a departure? Proposed: no override, spectators
  excluded, departure invalidates readiness, authority-controlled cancellation before loading.
- Does scarce stock use first accepted request or reservations/transfers? Proposed: first
  accepted edit, explicit rejection, no automatic transfer.
- Are loadout changes allowed only at existing campaign equipment steps? Proposed: yes;
  this screen never creates a new resupply opportunity.

These are already open in the campaign design, not requests to change confirmed direction.
Exact frame geometry/materials, preview resources, and localization glyph/fit behavior need
an installed-data engine inspection in COOP-007. Their absence from this design's runtime
evidence is recorded in the resource audit; it is not evidence that the native UI is infeasible.
