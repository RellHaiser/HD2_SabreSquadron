# Campaign preparation resource audit

Source audited at `7e033add` for COOP-003. Paths below refer to the hand port, not generated
`src/tier1`. This inventory establishes reuse candidates and mutation hazards; it does not
certify that any model was loaded or that the new screen ran in the engine.

## Campaign construction and reusable pieces

| Source / symbol | Observed resource or mechanism | Reuse and constraint |
| --- | --- | --- |
| [equipment_screen.cpp](../../src/game/equipment_screen.cpp), `BuildScreens` | Model keys `items selection`, `selection screen`, `description`; code-built `S_MenuDesc` / `S_ScreenDesc` | Native campaign scene route exists. This screen is not an HXML-authored layout. `BuildScreens` also ensures squad and auto-equips it; do not call it for co-op presentation. |
| Same, `BuildEquipmentPanel`; [equipment_panel.cpp](../../src/game/equipment_panel.cpp), `BuildDescription` | `equippanel` renamed `P_Equipment`; new `GB_Holsters`, `GR_BackPack`; places `head`, `uniform`, `back`, `shoulder`, `hands`, `drop`; `item00`…`item07` | Inventory visuals/containers are reusable through a presentation adapter. Templates need private meshes/materials before item texture changes. Existing handlers depend on control order/IDs. |
| Same panel description | `scroll13`, `scroll14`, `hlcpct00`, `hlcpct01`, `slider12`, `slider13`, arrows `barrow up23/down23/up24/down24`; `text_cpcty`, `hlcpct02`, optional `overload` and `description` | Four visible belt and four backpack entries with scrolling; preserve capacity/weight semantics. Optional frames must stay optional. Don't infer unlimited rendered inventory or automatic wrapping. |
| `C_EquipmentMenu::BuildSquadList` | `photo03` cloned with per-profile `faceImage`, `selector`, `GB_Soldiers`; four visible entries, horizontal step 0.106 | Reuse portrait/look-selection presentation; rebuild metadata and callbacks. Existing constructor deletes dead global squad entries and selects soldier index 0. New view must not do either. |
| `BuildSelectionScreen` | `photo03`…`photo06`, `selector`, `text_select one soldier`, `button00/01` | Existing single-soldier selection popup is a reference for mission restrictions, not the co-op ownership picker. |
| `BuildStore`; [store_list.cpp](../../src/game/store_list.cpp), `Describe` | `P_visualstore`, `filter`, `scroll01`, `item08` → `vi_template`, `item09` → `ammo`, `text_info00`, `itemselector`; six groups (small/middle/heavy weapons, explosives, dresses, other) | Reuse stock images/groups/scroll layout. Counts and requests must come from immutable view data rather than binding `C_ItemStock*`. `SetStock`/Take/Put paths mutate state. |
| `BuildStatsPanel`, `ShowSoldierStats` | `Panel02`; seven skill rows (including spelling `text_strenght`); name/customName; inventory/stats toggle | Preserve campaign skills, no multiplayer ranks/classes. Supply profile metadata through view data. Match exact resource spelling. |
| `BuildItemsScreen` | Existing title/inventory/stock/team/essentials labels; `bassign to all` is actually stats toggle; `baccept and continue` invokes AutoEquip; `bauto equip` invokes Continue | Resource names do not establish action semantics. New callbacks must be audited by symbol, never inferred from frame captions. |
| [menu_soldier.cpp](../../src/game/menu_soldier.cpp), `Setup`, `SetModel`, `Render` | Separate scene/camera/viewport, `mp_Na01`, uniform models `mp_` + model name; nine animation keys including `!static02NEW`, `!walkFN6c4`; face texture and item models | Reuse one selected-soldier preview. `mp_` is a model naming convention, not a reason to adopt multiplayer UI. Need installed model/animation/font data and native rendering QA. |
| [equipment_frames.cpp](../../src/game/equipment_frames.cpp) | Offsets, `HW1`, joints `c/d/f/o/u/q`, `backlink`, `mag1`; `def_holster_set`, nation-specific holster sets and model/dummy tables | Preserve attachment rules; no new attachment assets implied. Preview adapters must use existing item definitions, not hard-coded generic silhouettes. |
| [main_menu.cpp](../../src/game/main_menu.cpp), `OnMissionPicked`; [campaign.cpp](../../src/game/campaign.cpp), `GetMissionText` / `GetMissionImage` | Mission image plus campaign text IDs and paged/description text messages | Candidate briefing summary source. Text index 1's derived lines in the mission picker are a real reference, but verify the selected mission's available content. Do not substitute native MP map briefing. |
| [campaign.h](../../src/game/campaign.h), `C_CampaignVideo` | Briefing marker and Bink video filename in campaign sequence | Later local replay adapter should use campaign definitions; no video extraction or new authored briefing needed. |

## Global-state hazards and preserved rule behavior

| Path | What it does now | New adapter obligation |
| --- | --- | --- |
| `C_EquipmentMenu::OnStart`, `SetupStore`, `TakeSquadItems` | Resets/initializes profile stock; can remove unavailable items from every soldier | Host model initializes once per generation. UI construction/reconnection never resets campaign stock. |
| `EnsureSquad`, `BuildSquadList` | Loads/adds default soldiers; deletes dead entries in global mission squad | Model supplies legal roster; no side effects on view creation or snapshot receipt. |
| `C_EquipmentPanel::SelectSoldier` | Stores mutable pointer into `g_gameProfile.m_missionSquad->states` | Render from stable-ID view copy; viewing is neither claiming nor changing the global selected soldier. |
| `PutItem`, `RemoveItem`, `MoveItem`, `ReturnBackpack`, `ReturnItems` | Mutates selected soldier and can return gear to stock | Replace with commands; committed stock/loadout changes happen together in authority. Preview changes follow accepted snapshot. |
| `AutoEquip`, `DistributeRequired`, `Replenish`, preset changes | Iterates and changes multiple soldiers and shared stock | Rules may be extracted, but all human ownership rights are rechecked and target set is explicit. No “equip all” button that bypasses another human. |
| `HasRequiredItems`, `CountCarried` | Counts deployed carriers; axis items use `g_requiredCarrier`; uniform/diving requirements can clamp to squad size and mutate requirement records | Extract evaluation into host-owned validation with isolated scratch state; UI displays result, never calls this global-mutating checker. Preserve actual counting semantics and quirk audits rather than replacing with a naïve sum. |
| `IsSquadWithinWeight`, `IsSquadArmed`, Continue/Done messages | Weight check iterates squad; armed check skips nondeployed; missions 2/15 of campaign index 0 select one deployed soldier; Done clears autoEquip and stock1C and advances campaign | Do not silently “fix” legacy rule differences in UI. Host adapter owns validation/confirmation and campaign transition; Prepare/Ready is not `MENU_MSG_NEXT`. |

## Rendering, input, resource feasibility

[screen.h](../../src/game/screen.h) documents that menu descriptions are built in C++.
[menu_state.cpp](../../src/game/menu_state.cpp) `LoadModel` calls the model cache by key;
[control.cpp](../../src/game/control.cpp) creates controls on named frames. Thus code
descriptions alone do not provide missing text/quad frames. Audit actual required anchors
before constructing controls; old paths contain unchecked lookups. Reposition/clone installed
visuals or create original native frames; do not commit extracted campaign models/textures.

`S_ScreenDesc::AddControl` packs a 6-bit top-level index at bits 20–25; screen index uses
bits 26–31. Panel subcontrols use child fields (including bits 12–19 and 4–11). Allocate
separate new groups and check collision/child limits; appending a control in the middle of
the legacy panel can invalidate its constants. Pointer-sized callback payloads are local
engine implementation details, never protocol fields. Leave serialized profile layouts and
engine ABI untouched.

`C_EquipmentPanel::CreateSoldier` maps menu coordinates x=0…1, y=0…−0.75 into screen pixels
with −4/3 y scaling. `C_MenuSoldier::Setup` uses that viewport and its aspect ratio. This
supports a 4:3 reference composition; it does not prove arbitrary aspect ratios or safe
clipping after moving frames. Verify 1024×768 first, then widescreen and low resolution,
including hover bounds, preview viewport, scroll hit targets, and localized captions.

Existing `C_Screen::Unknown20` activates focused controls with Enter/Space/numpad Enter.
Focus is tied to control groups/cursor; full Tab traversal and keyboard item transfers are
new work. `CONTROL_MSG_DEACTIVATE` and `CONTROL_MSG_IGNORE_INPUT` support disabled groups,
but callbacks still need explicit model permission guards (including shortcut/drag handlers).
Do not treat hiding a button as authority enforcement. Caption/reason text remains readable
for read-only inventory; tooltips need a focus/selection alternative.

## Localization and original additions

Existing labels: title 0xA8D/0xA8E, Continue 0xA8F, recommended/replenishment 0xA90/0xAB1,
Back 0xA91, team 0xA92, stock 0xA93, inventory 0xAA8, essentials 0xAA9, weight 0xAB4,
missing item 0x804, overload 0x7E7, unarmed question 0x2531. Item names come from
`C_ItemDef::GetTextId`; use existing localization instead of copying English into shipping
controls. Numeric IDs above are observed in source, not newly allocated.

New symbolic text keys (COOP-007 reserves noncolliding numeric IDs after a full table audit):

| Key family | Required copy/parameters |
| --- | --- |
| `COOP_PREP_*` | Title, receiving state, editing, pending, committed, deploying, ended, return to preparation. |
| `COOP_OWNER_*` | You, host, AI, controlled by `{player}`, editing own/AI, read only, no assigned soldier. |
| `COOP_SLOT_*` | Claim/release, occupied, unavailable, dead, mission allows `{count}`, spectating. |
| `COOP_EQUIP_*` | Equip, Return, quantity/destination, out of stock, changed state, permission lost, capacity/weight/mission failure, wait for acknowledgement. |
| `COOP_READY_*` | Ready, Not ready, waiting for `{player}`, readiness invalidated, Deploy, blocking reason, confirmation/cancel/leave/end-session. |
| `COOP_ESSENTIAL_*` | `{carried}/{required} {item}`, carried by `{soldier}`, missing, validation pending, unarmed confirmation. |

Add original ownership/ready/pending markers with text alternatives; new briefing/rejection
text anchors and action frames; selected/editable/read-only focus styles. Existing portrait,
item and preview assets are reuse candidates, not new deliverables. No extracted art or
local game files are in this branch.

[game_texts.h](../../src/game/game_texts.h) uses numeric IDs and UTF-8 `id "text"` lines,
skips a UTF-8 BOM and rejects UTF-16. `Add` rejects duplicate IDs; do not assume an overlay
replaces an existing entry. [font.cpp](../../src/game/font.cpp) also has language-specific
font paths and `FontConfig.hxml` handling. UTF-8 parsing alone does not establish glyph
coverage, shaping, bidi support, or fit. Reserve space for longer labels, use pluralized
whole messages rather than English fragments, validate supported fonts/languages in-engine,
and use the existing fallback language policy for missing new strings.

## Installed-resource observation and limits (2026-11-18 session)

Read-only directory inventory found a local Steam installation at
`~/.local/share/Steam/steamapps/common/Hidden and Dangerous 2`. It contains `models.dta`,
`Maps.dta`, `Maps_U.dta`, `missions.dta`, `Scripts.dta`, `LangEnglish.dta`, `SabreSquadron.dta`,
patch archives, executables and DLLs, loose `Maps/`, language directories including
`Text/english/TEXTY.txt` and `TEXTY_DD.txt`, and campaign `Gamemovie/Brief*.bik` files.
There is no loose `Models/` or `Tables/` directory. Presence of archives is not proof of
their decoded member/frame inventory, version correctness, or compatibility with the port.

This task did not decode archives, extract assets, start the game, or change the installation.
The model keys/frames above are **source-observed**; their geometry, texture look, missing
variants and runtime availability remain **unverified in installed data**. This is narrower
than saying game data is absent. No exact in-game screenshot comparison or live UI run is
claimed. COOP-001 owns runnable baseline/toolchain evidence. COOP-007 must load these
resources in its native harness and record rendering/input evidence before component review.

The original [HTML mockup](campaign_preparation.html) has no external fonts, textures,
scripts or dependencies. It can be reviewed with just a browser. It is the feasible preview
for this design phase; there is no concrete constraint justifying a multiplayer UI fallback.
