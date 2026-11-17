# Campaign co-op shared contracts

COOP-002 defines a documentation-only foundation for COOP-004 through COOP-010.
This document specifies behavior and logical interfaces, not a C++ ABI or a wire
layout. No new shared header, shipping mode, transport handler, or save format is
introduced here. Implementers use the port's `C_`/`S_`, PascalCase method and `m_`
member conventions. The source audit below refers to base commit `7e033adde95881baad06c6877cbbd6eef8197d96`.

The [campaign design](base_game_coop.md#goal-and-agreed-behavior) is the product
authority. Four humans maximum, original campaign soldiers/rules, AI for unclaimed
soldiers, per-human loadout choice, and campaign-style preparation are confirmed.
The [open policies](#open-policies) remain proposals. Serialization choices and the
concurrency rules below are engineering contracts; they do not approve those policies.

## Identity and session context

Names in this table denote distinct value types even when an implementation uses
the same integer representation. Zero is invalid for generated tokens/counters;
an absent value is explicit, never a valid player slot or soldier. Host-generated
tokens must not repeat in their stated scope. Counter overflow requires a new
enclosing generation/session before reuse; comparison is equality or increasing
order within that scope, never unchecked wrapping arithmetic.

| Logical value | Scope, allocator and lifetime | Must not be used as |
| --- | --- | --- |
| `SessionId` | Host allocates a fresh opaque token for each hosting lifetime, including resume after process restart. | Saved connection identity, password or proof of authority. |
| `CampaignId` | Host-owned campaign instance identity; distinct campaigns using the same roster have distinct IDs. Preserved by future co-op saves. | A map filename or session token. |
| `MissionKey` | Campaign/content catalog key for the original campaign mission, including campaign step and content version. | A process pointer to `C_MissionDef`, native MP map index, or mission incarnation. |
| `MissionGeneration` | Session-scoped counter advanced before each world reconstruction: first start, restart, load, or map transition. | A legacy one-bit session parity. |
| `PreparationGeneration` | Session-scoped counter advanced for a new equipment/preparation transaction context, including reopening after cancellation. | A screen instance or mission generation. |
| `SoldierId` | Campaign-scoped stable roster identity. COOP-004 maps a validated unique roster `profileIndex` into an explicit ID/catalog; retain it across missions and actor reconstruction. | Squad-list position, human owner, actor ID or connection slot. |
| `PlayerId` | Session-scoped connection incarnation allocated on admission; never reused when a transport slot reconnects. Host's local participant also has one. | Display name, account identity or durable campaign owner. |
| `ActorRef` | `(SessionId, MissionGeneration, actorId, slotUse)` from the host's current actor binding catalog. Legacy `slotUse` is only a bounded reuse discriminator. | A soldier or a player; the legacy 8-bit counter alone cannot establish freshness. |
| `RequestId` | Increasing sequence per `(SessionId, PlayerId)` across request families for that connection lifetime. | A global ordering of commits or a client-chosen editor identity. |
| `OwnershipRevision` | Session-wide ownership catalog revision; advanced atomically for a transfer or eligibility/binding change. | A preparation stock revision. |
| `ControlRevision` | Per-soldier session-scoped counter advanced on owner, effective controller, eligibility, or actor-binding change, even a change back to a former owner. | Player slot, packet sequence, or resettable actor-local counter. |
| `PreparationRevision` | Commit counter within one preparation generation; covers squad, stock, loadouts, rights and validation inputs. | A readiness notification sequence. |
| `ViewRevision` | Sequence within one preparation generation, also advances for ready, presence, phase and result-relevant view changes. | The revision to which a player has agreed to deploy. |
| `SnapshotId` | Unique transfer token bound to the full snapshot manifest and its baseline revisions. | A save-file ID or proof that world reconstruction finished. |

The catalog must reject duplicate/missing roster identities. If a mission introduces
a soldier outside the validated roster, allocate and retain a campaign identity
explicitly; do not silently identify it by its actor slot. The persistent ID encoding
and save migration belong to the later save task. COOP-004 tests the catalog in memory.

`S_CampaignSessionContext` logically exposes `SessionKind` (solo, legacy multiplayer,
campaign co-op), campaign identity/rules, transport server/client flags, local
`PlayerId`, and the authoritative difficulty/content descriptor. A listen host may
be both server and client. A client flag does not grant simulation authority.

Keep `IsLocalGame()`, `IsServer()` and `IsClient()` unchanged. Narrow adapters answer
`UsesCampaignRules`, `HasSimulationAuthority` and `IsLocallyOwned(SoldierId)` using
the context. Solo and native multiplayer keep their old code paths. Do not append a
fourth unchecked `PLAY_MODE_*` value or add fields to serialized legacy structs to
carry this context. Campaign co-op uses the campaign rules with a separate session kind.

## Lifecycle and transition boundaries

| From | Host operation and preconditions | To / invalidation |
| --- | --- | --- |
| No session | Create explicit campaign co-op context and identity catalog. | Setup; transport peers are not yet campaign participants. |
| Setup | Negotiate/admit each participant; install a valid campaign equipment context if this step permits equipment. | Preparation with a fresh preparation generation. |
| Preparation | Authoritative policy permits deployment, all campaign equipment checks pass, no unresolved transaction, and expected preparation revision matches. | Frozen; capture an immutable deployment manifest at that revision. |
| Frozen | Host begins reconstruction, advances mission generation and clears old input/world bindings before actor creation. | Loading; load only the captured squad/loadouts/rules/content. |
| Loading | Host and each required participant acknowledge the same snapshot/manifest and new bindings. | Running; only now enable input and simulation. |
| Frozen or Loading | Abort the attempt; stop input and discard partial worlds/queued messages. | Fresh preparation generation if this campaign step permits it, otherwise Setup or Closed; never unfreeze an old generation. |
| Running | Host ends the mission according to original campaign rules; stop further input. | Ending; campaign results are committed once by the campaign authority adapter. |
| Ending | Follow the original next campaign step. | Setup, fresh Preparation when allowed, or Closed. Video/debrief/squad screens are not forced into an equipment phase. |
| Any live phase | Session termination; host loss in the initial proposed policy. | Closed; reject subsequent mutation/input. |
| Running | Future authorized load/restart transaction. | Loading with a fresh mission generation; full save/reconnect work is a later gate. |

These are logical phases, not changes to `ROUND_*` or `GAME_MODE_*`. The first
development run can supply a fixed committed manifest; it need not implement a
production menu or settle readiness policy. Loading completion is not mission
completion. No transition implies a new soldier, respawn, replenishment or free life.

Membership changes serialize with deployment freeze. If the set required at the
barrier changes, the adapter must explicitly abort/re-evaluate the barrier under
its policy; a departure is never counted as an acknowledgment. Timeouts cannot
silently turn missing state into readiness. The first playable gate admits peers
before loading; it rejects a new admission while Running with `JoinNotSupported`.
Receiving current preparation state alone never authorizes joining a running world.

## Ownership model and requests

An ownership record contains soldier identity, eligible/deployed/alive flags,
optional owner, optional current actor binding, scripted-control suspension, and
control revision. Player presence and local camera selection are separate records.
The host maintains both soldier-to-owner and player-to-soldier indexes atomically.

Invariants:

1. At most one human owns a soldier; a player owns at most one soldier. Connected
   humans are capped at four; mission squad restrictions can allow fewer soldiers.
2. An eligible, living, unowned deployed soldier uses squad AI on the host. Guests
   render replicated state; they do not independently run authoritative squad AI.
3. Ownership is not effective input authority while scripted control is active,
   the soldier is dead/ineligible, loading is incomplete, or the session is frozen.
4. Transfer changes the controller, not health, gear, skills, position, mines,
   campaign identity, action effects, or the number of actors/lives.
5. Script entry retains the assigned human but suspends input and advances control
   revision. Script exit advances it again and restores the current owner or AI,
   rather than a cached pre-script controller. Disconnect during scripting clears
   that owner without cancelling the script.

`C_CampaignOwnership` has the logical operations `GetSnapshot`, `FindOwner`,
`FindSoldier`, `ResolveActor`, `ApplyRequest`, `OnDisconnected`, `OnDeath`,
`SetScripted`, and `BeginMissionGeneration`. These are model operations; they do
not dereference a game actor or replace a controller. Adapters consume committed
changes. Trusted lifecycle events cannot be invoked by decoding a client opcode.

Every client mutation contains session, request ID, expected ownership revision,
and relevant expected control revisions. The dispatcher derives `PlayerId` from
the authenticated transport connection incarnation and compares any echoed editor
field with it. The local host uses the same dispatcher path.

| Request/event | Preconditions | Atomic result |
| --- | --- | --- |
| `Claim(soldier)` | Selection policy allows claim in this phase; current eligible soldier is unowned and not scripted. In Running it must also be living, deployed and bound. Requester owns none. | Assign owner, increment target control revision and catalog revision. |
| `Switch(from, to)` | Requester owns `from`; `to` passes claim rules; both expected control revisions match. | Release `from` and claim `to` together or change neither; advance both control revisions and one catalog revision. |
| `Release(soldier)` | Requester is current owner and phase/policy allows release. | Clear ownership; preserve the soldier; use AI when eligible and not scripted. |
| Disconnect | Host observes the exact current connection incarnation. | Clear that player's ownership once; invalidate input and preparation rights; preserve equipment and any active script. Repeated event is a no-op. |
| Death/ineligibility | Trusted simulation event. | Invalidate effective input, clear ownership on death, advance affected revisions; no automatic claim of another soldier or revival. Soldier selection/spectator presentation is policy. |
| World reconstruction | Host advances mission generation. | Remove actor bindings and suspend input, invalidate all affected control tokens; rebuild bindings and revalidate assignments before barrier completion. Preserve roster identities, never restore saved connection slots. |

Claim authorization before deployment can refer to roster eligibility without an
actor. Actor binding is mandatory for running input. Forced host assignment of
another player's soldier is not an ordinary claim and is outside this interface
until the selection policy is confirmed.

An accepted result includes committed revisions and the complete affected records;
a rejected result includes a stable reason and an authoritative current snapshot
or a resync descriptor. Reasons include `WrongSession`, `WrongGeneration`,
`StaleRevision`, `NotAdmitted`, `NotOwner`, `AlreadyOwned`, `AlreadyControlsSoldier`,
`InvalidSoldier`, `Ineligible`, `Scripted`, `InvalidPhase`, and `PolicyRequired`.
Unknown IDs are rejected before indexing legacy tables.

For running input/actions the authority tuple is `(SessionId, MissionGeneration,
PlayerId, SoldierId, ActorRef, ControlRevision)`. Validate it against the sender and
current mapping before applying movement, inventory, fire, healing, vehicle control
or queued actions. Check at application time as well as enqueue time. Transfer drains
or invalidates old controller/net-object queues, changes handlers and replication
ownership, then updates local camera, first-person view, inventory and selection.
The old owner cannot keep authority by retaining the same actor ID. The mapping
commit and this adapter handoff form a barrier: disable input until both complete.

## Preparation model and UI boundary

`I_CampaignPreparation` is a logical interface implemented by a fake for COOP-007
and later by a transport-backed adapter. COOP-005 owns model implementation and
COOP-011 owns the shared identity/DTO/interface declarations; this document intentionally adds no headers
requiring an unverified target build. The first implementation adds the agreed
typed declarations at the reserved paths below, without exposing engine/profile
pointers or compiler-dependent objects as wire formats.

| Method | Contract |
| --- | --- |
| `GetSnapshot()` | Return a lifetime-safe immutable value/view of one complete published revision, with Loading/Resyncing/Disconnected status when no usable snapshot exists. No writable `S_SquadSoldier*`, `C_ItemStock*` or profile globals. |
| `SubmitEdit(request)` | Queue an atomic equipment operation. Return a local submission token or local validation failure; acceptance arrives as a correlated authoritative result. Queued does not mean committed. |
| `SubmitOwnership(request)` | Route claim/switch/release to the ownership model. UI selection alone never changes deployed squad or owner. |
| `SetReady(request)` | Set/clear this participant's readiness for the stated preparation revision. Never mutate equipment or decide the deployment quorum. |
| `RequestDeploy(request)` | Host-only proposal referencing the expected committed revision; policy and campaign validators decide whether freeze can occur. |
| `RequestResync()` | Request a complete preparation snapshot after a gap; disable mutations until installed. |
| `PollEvent()` | Deliver `SnapshotChanged`, `RequestResult`, `ResyncRequired`, or connection/phase status on the UI thread; no callbacks into a destroyed screen. |

The screen owns editor selection, filters, hover, scroll and pending proposals.
It may render an explicit local preview, but cannot present that preview as committed
stock or deploy it. Snapshot values outlive the draw/input operation; replacing a
snapshot cannot leave stored pointers into the old model. On screen destruction,
detach event delivery; an already submitted request may still commit, and reopening
reads authoritative state. Never "undo" by blindly returning local items to stock.

`S_CampaignPreparationSnapshot` contains:

- Session/campaign/mission keys, preparation generation, preparation/view revisions,
  phase, local participant/host identity, connection/synchronization status, difficulty,
  briefing/localization references and content/rules descriptor.
- The four presentation slots with optional soldier, eligibility/unavailability reason,
  deployment/mission restrictions and soldier display/profile references. Fewer eligible
  soldiers do not create substitute soldiers. An empty slot has no fake `SoldierId`.
- Ownership catalog revision and per-soldier owner, control revision and current
  `CanView`, `CanEdit`, `CanClaim`, `CanRelease` capabilities with denial reasons.
  Ownership display distinguishes AI, human owner and scripted suspension.
- Complete committed soldier loadouts with item catalog keys, quantities, placement,
  ammunition/condition data required by the existing campaign rules, weight/capacity,
  skills and validation findings. Preparation item references are scoped to the
  preparation catalog/revision, not live-world pointers or unchecked legacy UIDs.
- Shared stock counts, availability and allowed destinations/constraint summaries, mission essentials with required/carried
  counts and deficits, and campaign validation findings at that same revision.
- Participant presence and ready-at-preparation-revision values; deploy capability
  and blockers, including unresolved policy. Ready badges do not imply a chosen quorum.

Edit requests carry `(SessionId, PlayerId echo, RequestId, PreparationGeneration,
expected PreparationRevision)` plus an explicit target set and each target's
expected control revision. The host derives the actual editor. Operations are
`TakeFromStock`, `ReturnToStock`, `MoveWithinLoadout`, `ApplyPreset`, and
`AutoEquip(targets)`, with bounded counts, valid catalog/placement keys and explicit
parameters. A panel never submits an arbitrary serialized replacement profile.
Direct transfers between soldiers and item reservations are excluded pending policy.
`ApplyPreset`/`AutoEquip` cannot infer an implicit whole-squad target.

Permissions at commit time are exactly:

| Editor | Own soldier | Unclaimed eligible AI soldier | Another connected human's soldier |
| --- | --- | --- | --- |
| Host participant | Edit | Edit | View only |
| Guest participant | Edit | View only | View only |

Mission/phase restrictions can remove permission, never broaden this table. A host
with no claimed soldier can still edit eligible AI soldiers. Ownership changes are
serialized with preparation transactions: advance preparation revision and invalidate
pending old-owner edits before publishing new rights. Model tests inject the ownership
snapshot/catalog; production integration must commit the two models through a single
authority queue, not update rights asynchronously after accepting edits.

### Atomic edits, results and readiness

The host serializes mutations. This first contract deliberately uses one aggregate
preparation revision: two requests based on the same revision can both be submitted,
but after one changes state the other receives `StaleRevision` and may be proposed
again from fresh state. The UI never retries a rejected choice automatically.
This permits simultaneous editors without silent merging or stock duplication.

For each edit, in order:

1. Validate sender/admission, session/generation/phase and bounded syntax; check the
   duplicate sequence rules below. Verify the expected preparation and target control
   revisions against the current models, not the state at screen-open time.
2. Check all target permissions and campaign catalog/placement/count constraints.
   Apply the complete proposal to a private candidate of loadouts plus shared stock.
   Any invalid target rejects the whole bulk request; no partial auto-equip.
3. Run the campaign equipment rules (weight/capacity, compatible ammo, stack/slot limits,
   uniform/weapon restrictions). Preserve the existing distinction between a permitted
   intermediate edit and a deployment blocker: taking off a required item can expose
   an essentials deficit, but freeze must fail until it is repaired. COOP-005 audits
   which original checks are edit rejection versus Continue/deploy checks.
4. Commit candidate loadouts and stock together, increment preparation and view
   revisions once, invalidate readiness for the previous preparation revision, and
   publish one result/state change. Failure leaves every count and revision unchanged.

For each stock-controlled item type, `stock + quantities across all selected loadouts`
is conserved by transfers, with stack splitting/ammo conversion accounted for by the
existing item rules. Only explicit trusted campaign supply/roster transitions change
that total; opening a screen, disconnecting, replaying a request or restoring a view
does not. Existing campaign stock containers must be normalized exactly once when
creating the authority model; the model must not count the same equipped items in
both stock and loadouts. Preserve mission-specific carrier rules and ammunition/stack
semantics rather than replacing them with a simple item-count sum. Never run `TakeSquadItems`, `DistributeRequired`, or whole-squad auto-equip
as a side effect of binding the new screen.

`S_CampaignRequestResult` identifies session, player, request, request family,
outcome (`Accepted`, `Rejected`, `Duplicate`, `ResyncRequired`), reason, committed
preparation/view/ownership revisions, and changed records or full snapshot reference.
Equipment reasons additionally include `Frozen`, `StockUnavailable`, `InvalidItem`,
`InvalidPlacement`, `InvalidCount`, `CapacityExceeded`, `MissionRestriction`, and
`RequiredItemsMissing` for deployment. Field-level findings identify soldier/item/slot
using catalog values and localizable reason codes, not arbitrary network UI strings.
A result referencing a newer snapshot cannot enable editing until that state is installed.

Deduplication is shared by request families: retain the highest consumed RequestId
per admitted player and a bounded cache of canonical request content plus its result.
A cached identical duplicate replays its original result with no side effects;
same ID/different content is `RequestConflict`. A consumed ID older than the cache is
`ResyncRequired`/`RequestExpired`, never applied again. A higher ID consumes its
sequence even on semantic rejection. Lower reordered requests are expired rather
than applied out of order; reliable ordered mutation delivery is preferred. A new
generation invalidates old payloads even if their result is cached. Cache size,
request rate and sequence limits are bounded by COOP-006; no unbounded per-peer storage.

Ready requests refer to the current preparation generation and committed preparation
revision. Setting readiness changes view revision only, so one player's ready toggle
does not invalidate another's. Any change to squad/loadouts/stock/rights/rules or
participant membership invalidates previous readiness and advances preparation
revision. Repeated identical ready state is a no-op. Freeze closes the edit gate
atomically with capturing the manifest; an edit racing freeze either commits first
and makes its expected deploy revision stale, or is rejected `Frozen`.

## Protocol compatibility and snapshot boundaries

The campaign protocol is an explicit opt-in extension. Native `S_JoinRequest` remains
0x54 bytes, native version fields retain their meanings, and ordinary singleplayer
and multiplayer emit no campaign extension traffic. A successful native connection
is not campaign admission. `SessionId`/`PlayerId` are routing/freshness tokens, not
cryptographic authentication; authorization uses the transport's verified sender binding.

The codec component owns these logical messages:

| Family | Required information / boundary |
| --- | --- |
| `Hello` | Mod identifier, supported campaign protocol major/minor range, required/offered feature sets, supported rules/content descriptor and campaign session kind. No mutation permitted. |
| `Accept` / `NegotiationAck` | Chosen version/features, authoritative session identity and content/rules descriptor, connection-incarnation PlayerId and limits. Client echoes the chosen session/version/features in its ack before campaign admission/state installation. |
| `Reject` | Bounded reason: `ModRequired`, `UnsupportedVersion`, `MissingFeature`, `ContentMismatch`, `RulesMismatch`, `Malformed`, `Timeout`, `SessionFull`, `JoinNotSupported`. No legacy-rank fallback. |
| Ownership/preparation request/result | The identity/revision fields and typed operations above; client-supplied player/editor must match transport sender. |
| `PreparationSnapshot` / delta | Complete model above at one view/preparation/ownership baseline; deltas name their exact base view revision. |
| `BeginMission` / initial manifest | Mission key/content/difficulty, mission generation, frozen preparation revision, selected roster IDs/stats/loadouts, actor binding catalog and required participant set. |
| `SnapshotAck` / `StartSimulation` | Exact snapshot ID/manifest identity, mission generation and installed baseline revisions; host starts only after the required barrier is satisfied. |
| Input/action envelope | Current authority tuple plus bounded typed payload; no unguarded legacy actor message can bypass this check in campaign mode. |

Negotiation state per transport connection is `Connected -> AwaitHello -> AwaitAck
-> Admitted`, or terminal `Rejected/Closed`. Only bounded negotiation messages are
decoded before admission; receiving a mutation or world/input packet early rejects
it without touching the models. Accept only a matching mod/major and an intersection
of supported minors with all required features understood on both ends; select the
highest common minor. Unknown required features reject; optional unsupported features
are disabled. Initial codecs support one explicit version rather than guessing how
to decode another. Content descriptors must cover relevant campaign, mission, item
and soldier definitions, not just map names. Hash/version encoding is a codec decision
documented in fixtures, never an assumption of equal local files.

The live attachment must be implemented as follows, subject to COOP-006's dispatch
audit before allocating a numeric extension command:

- Reserve and document an unused explicit `NET_MESSAGE_GAME` command ID; do not
  repurpose an existing opcode or widen the five-bit native message type.
- In an explicit campaign session, intercept that command before ordinary server/client
  game-command dispatch and quarantine native join/spawn/respawn/state commands that
  would otherwise run before negotiation. Unmodded peers time out/reject before spawning.
  Do not use a new native join struct or guess that a vanilla client ignores an opcode.
- Carry a length-delimited, versioned campaign envelope inside the negotiated command.
  No codec may cast C++ structs, pointers, `bool`, enum storage or `size_t` onto the wire.
  COOP-006 specifies exact integer widths, byte/bit order, lengths, limits and fixtures.
  Validate remaining bits before reads, including nested counts and arithmetic overflow;
  `C_BitStream::IsValid()` after an unchecked read is insufficient as a decode strategy.
- Actor-object/control-state paths also require the authority envelope or an equivalent
  negotiated, generation-tagged adapter before invoking legacy handlers. Tagging only
  reliable game commands leaves unreliable control bits and actions exposed.
- COOP-006 delivers tested codecs/negotiation plus a concrete attachment plan; COOP-008
  consumes its guarded input boundary, COOP-009 supplies world/barrier hooks, and COOP-010
  installs/tests the live dispatch and admission gates together. No earlier component
  advertises a playable campaign session.

Transport sessions and native parity do not replace these campaign generations.
Codec limits must include max four participants, campaign squad bounds, catalog/count
validation, max payload/strings/chunks, max in-flight snapshots, timeout and rate limits.
Malformed/truncated/oversized/unknown-required messages fail closed with no partial
model mutation. Duplicate requests follow the model rules; no decoder retries a mutation.

A preparation snapshot is one immutable authority-queue cut of stock, all loadouts,
rights, validation, presence and readiness. Install it atomically; do not combine stock
from revision N with loadouts from N+1. Delta gaps or mismatched generations force a
complete resync and disable editing. Apply only increasing view revisions in the same
generation. A correlated duplicate result does not roll the view back to its old snapshot.

Initial-world snapshots include actor bindings, transforms/health, inventories, AI,
vehicles, dynamic actors, script/objective baseline and replication state required at
mission start, in addition to the manifest. A manifest alone is not the world. Chunked
snapshots have an ID, bounded manifest/total size, chunk indexes/count, integrity check
and baseline revisions; install only after the complete validated transfer. Changes
after the baseline must be buffered and replayed in order or require a fresh snapshot.
An acknowledgment means reconstruction and binding installation succeeded, not just
that bytes arrived. Failed reconstruction cannot enable input.

Saves contain durable campaign/soldier/world state; they never grant authority to a
saved PlayerId, slot, pointer, or old actor binding. Late join/reconnect during play
needs a current-world snapshot including evolved scripts/objectives, vehicles and
inventory; neither a preparation snapshot nor replaying original spawns suffices.
Full save/load and mid-mission admission remain separate tasks after COOP-010.

## Legacy coupling audit and adapter allocation

These are source findings, not observed host/guest behavior. References name symbols
so line drift does not invalidate the audit. New related call sites discovered during
implementation must be recorded by the owning task; this is a bounded starting map,
not proof that every gameplay interaction already supports co-op.

| Source / symbol | Existing assumption and risk | Assigned adapter obligation |
| --- | --- | --- |
| [`profile.h`](../src/game/profile.h), `S_SquadSoldier`, `FindSoldierState`; [`profile.cpp`](../src/game/profile.cpp), `IsCooperativeGame`, `GetDifficulty` | Roster `profileIndex` exists; cooperative detection selects native MP settings, and nonlocal difficulty can select multiplayer rules. | COOP-004 owns external context/catalog. COOP-009 reads campaign difficulty/rules through it; retain legacy layouts and native checks. |
| [`human_control.cpp`](../src/game/human_control.cpp), `AssignPlayerController`, `SetScripted`, `GetPlayerName`, `GetProfileUnknown24` | Net ID selects local input, names/ranks index players; script exit uses local selected player. | COOP-008 resolves owner/local presentation, preserves script suspension, campaign names/skills and AI fallback. |
| [`player_net.cpp`](../src/game/player_net.cpp), `StampActivity`, `ApplySoldierClass`, `TickLocal`, `FlushMessages`, net handlers | Actor net ID indexes activity/stats/rank; actor messages and control bits lack campaign authority generations. | COOP-008 resolves actor-to-owner only for connection activity, bypasses MP class assignment, and guards all action/control paths. Actor-target IDs stay actor IDs. |
| [`human_controller.cpp`](../src/game/human_controller.cpp), `Attach`, `ExecuteCommand`; [`human_body.cpp`](../src/game/human_body.cpp), `Create`, `SetPlayerControlled`; [`remote_controller.cpp`](../src/game/remote_controller.cpp), `OnMessage` | Net-object handlers attach to humans; local slot comparisons and remote class messages survive a controller change. | COOP-008 updates handlers, input queues and body flags at handoff; reject obsolete class/control messages in campaign mode. |
| [`player_controller.cpp`](../src/game/player_controller.cpp), `UseMedkit`; [`human_combat.cpp`](../src/game/human_combat.cpp), `Hit`; [`net_players.cpp`](../src/net/net_players.cpp), `OnPlayerKilled`; `net_game.cpp`, `GetPlayerHuman` | Victim/attacker actor IDs flow to native player scoring; medic score indexes actor ID. | COOP-008 preserves actor damage attribution/campaign stats without native ranks/lives or invalid player-array indexing. No-owner AI must remain valid. |
| [`controller_messages.cpp`](../src/game/controller_messages.cpp), request codecs; [`human_items.cpp`](../src/game/human_items.cpp), `RequestMoveItem`, `GetEquipmentState` | Inventory/interaction targets are world actor IDs; local/network branches affect equipment use. | COOP-008 authenticates the requesting controller, validates target separately and audits healing/transfers. Do not replace every target actor ID with a PlayerId. |
| [`team.cpp`](../src/game/team.cpp), `SwitchTo`, `ReturnControl`, `HasMemberInPlayerSlots`; [`interface.cpp`](../src/game/interface.cpp), [`first_person_view.cpp`](../src/game/first_person_view.cpp) | One active local player, player-slot range membership, and local-only UI gates. | COOP-008 separates local camera/editor selection from ownership and full deployed squad; refresh inventory/weapon view on handoff. |
| [`car_seats.cpp`](../src/game/car_seats.cpp), [`car_hits.cpp`](../src/game/car_hits.cpp), [`car_callbacks.cpp`](../src/game/car_callbacks.cpp), [`car_update.cpp`](../src/game/car_update.cpp), [`car_net.cpp`](../src/game/car_net.cpp) | Driver net ID equals local player slot in vehicle camera/control/replication paths. | COOP-008 inventories/guards local-driver and input authority checks; full vehicle behavior is a later interaction acceptance gate, not established by model tests. |
| [`actors_module.cpp`](../src/game/actors_module.cpp), `Unknown28`, `TickRemovals`; [`spawn_point.cpp`](../src/game/spawn_point.cpp), `Spawn`, `SetupSpawned`, `CreateSoldier` | Player-slot range selects human dispatch; removal indexes players by actor ID. Network spawn uses `actorSlot = slot`, MP zones, player stats and `players[slot].human`; spawn message has 8-bit slot/actor-slot/use fields. | COOP-008 owns controller dispatch/removal safety. COOP-009 owns actor allocation/catalog, campaign spawn state and original points; never widen fields silently. AI cannot require a fake connection. |
| [`game.cpp`](../src/game/game.cpp), `Start`, `Stop` | Local-only campaign spawning/writeback, contiguous player-role actors, nonlocal squad deletion and warmup selection. | COOP-009 uses explicit host campaign authority and catalog iteration, preserves roster/stock and avoids MP warmup/lives/respawn. |
| [`game_rounds.cpp`](../src/game/game_rounds.cpp), `OnPlayerJoined`, `OnPlayerLeft`, `Update` | Join spawns MP soldier; leave removes coop member, destroys mines/actor, clears stats; update includes round/respawn processing. | COOP-008 supplies the disconnect ownership event; COOP-009 alone edits this file to call it before native destructive logic in campaign mode, bypass spawn/round flow and preserve AI/actor state. |
| [`game_app.cpp`](../src/game/game_app.cpp), `StartStep`, `StartNextMap` | Original campaign screen sequence differs from network map rotation; `StartNextMap` calls `SetupMultiplayerMission` and resets player stats. | COOP-009 alone owns campaign lifecycle hooks/manifest/barrier integration; preserve original steps, prohibit MP map/rank reset for campaign. |
| [`equipment_screen.cpp`](../src/game/equipment_screen.cpp), `TakeSquadItems`, `AutoEquip`, `DistributeRequired`, `HasRequiredItems`; [`equipment_panel.cpp`](../src/game/equipment_panel.cpp), `SelectSoldier`, `PutItem`, `ReturnItems` | Direct global squad/stock mutation and whole-squad bulk operations; validity checks are partly on Continue. | COOP-005 audits/extracts rules into candidate-state validators; COOP-007 wraps visuals with snapshot/proposals. Existing solo screen behavior stays isolated. |
| [`script_vm.cpp`](../src/script/script_vm.cpp), `GetPlayerTeamForMode`, `OP_FRM_GetNearestPlayer`; [`ai_controller.cpp`](../src/game/ai_controller.cpp), `UpdateNetwork` | Native co-op chooses `C_CoopTeam`; replication has role gates. | COOP-009 uses the whole deployed campaign squad including AI for player/team queries and host AI replication; classify script effects as authoritative or presentation before reuse. |
| [`net_game.h`](../src/net/net_game.h), `S_JoinRequest`; [`net_messages.cpp`](../src/net/net_messages.cpp), `HandleServerMessage`; [`net_game.cpp`](../src/net/net_game.cpp), `DispatchNetMessage`; [`net_commands.cpp`](../src/net/net_commands.cpp), `HandleServerCommand` | Fixed native join layout/version; five-bit type plus 32-bit command ID; native command 0 reads slot/soldier and calls `OnPlayerJoined`. | COOP-006 audits/records a safe extension. COOP-010 installs negotiation/quarantine before native join or actor input can run. Merely adding a Hello codec is insufficient. |

### Reserved file ownership and sequencing

Paths below are agreed reservations, not files already supplied by this document.
Tasks keep their dependency status `planned` until the required results merge.

| Task | Owned implementation / tests | Consumer and serialization boundary |
| --- | --- | --- |
| COOP-011 | `src/game/campaign_contracts.h`; `_re/harness/campaign_contracts/` | Shared distinct identity types, request/result/snapshot DTOs and preparation interface. Compile in intended MSVC x86 target; review consumers before implementation branches bind to the merged header. No gameplay/model/codec implementation. |
| COOP-004 | `src/game/campaign_session.{h,cpp}`, `campaign_ownership.{h,cpp}`; `_re/harness/campaign_ownership/` | Consume merged COOP-011 declarations. Keep engine-independent; shared header changes require consumer coordination. |
| COOP-005 | `src/game/campaign_preparation.{h,cpp}`; `_re/harness/campaign_preparation/` | Consume merged COOP-011 DTO/interface and an injected ownership catalog view; live model composition is later integration, so no COOP-004 implementation dependency. Legacy equipment validator extraction requires prior scope acknowledgment. |
| COOP-006 | `src/net/campaign_protocol.{h,cpp}`; `_re/harness/campaign_protocol/` | Consume merged COOP-011 logical contracts, own wire schema/limits/fixtures. Engine-independent codecs first; record attachment plan, reserve numeric ID after audit. No concurrent native dispatch edits. |
| COOP-007 | `src/game/campaign_preparation_screen.{h,cpp}`; `_re/harness/campaign_preparation_ui/`; original resources agreed by COOP-003 | Fake implements the same preparation boundary. Fake/live interface declarations come from merged COOP-011; no dependency on COOP-005 model implementation. `equipment_screen.*` stays with its solo route; coordinate any panel/frame extraction. |
| COOP-008 | Ownership/controller/input/presentation hotspots above; new `src/game/campaign_control_adapter.{h,cpp}`; existing human harness additions | Own `actors_module.cpp` controller dispatch/removal safety and player/net ownership consumers. Export disconnect/handoff helpers; do not edit `game_rounds.cpp`, `spawn_point.cpp`, `game_app.cpp` or profile layout. |
| COOP-009 | `game.cpp`, `game_app.cpp`, `game_rounds.cpp`, `spawn_point.cpp`, narrow profile-rule and script/AI changes above; new `src/game/campaign_mission_adapter.{h,cpp}`; lifecycle harness | Sole writer of overlapping lifecycle files; calls COOP-008 helpers through an agreed adapter API. Can develop against that API; final combined integration follows COOP-008 merge and repeat checks. |
| COOP-010 | Development launch and live admission/dispatch attachment, initial-world integration and runtime evidence | Starts after COOP-008/009 merge. Owns `net_game.cpp`, `net_commands.cpp`, `net_messages.cpp` attachment edits then; coordinates fixes with prior task owners. |

COOP-011 is the deliberately small compiled-declaration foundation after COOP-002.
COOP-004/005/006/007 depend on its merged header and can then implement independently;
COOP-007 also needs COOP-003's UI result. Model/codec tests use injected/fake catalog
views, not another unmerged implementation. No duplicate public DTO definitions or
unfinished feature-branch stack enters `main`. The coordinator approved this dependency
refinement during COOP-002 review preparation; the task index records the new gate.
COOP-009's shared lifecycle-file edit sequence and COOP-010 attachment remain serialized,
even though controller/lifecycle components can be developed separately.

## Required behavioral evidence

These scenarios specify downstream tests; they have not been executed by COOP-002.
All model rejection cases assert unchanged state, conserved stock and no actor effects.

| Owner | Scenario | Required result |
| --- | --- | --- |
| COOP-011 | Compile all shared declarations in one standalone translation unit; type-check a minimal fake preparation implementation and representative DTO construction. | MSVC x86 compile succeeds without engine globals, pointers in snapshots, duplicate identity types or wire/legacy ABI assumptions. |
| COOP-004 | Two peers claim one soldier at the same revision; one peer claims two; invalid/duplicate roster ID; restricted squad. | One exclusive owner, explicit rejection, consistent reverse index; no extra soldier. |
| COOP-004 | Failed switch, release/disconnect twice, death, scripted entry/exit with intervening disconnect. | Atomic switch/no partial release; live soldier preserved; no revival; script restores current owner/AI. |
| COOP-004/008 | Reuse transport slot, actor slot/use counter; switch away/back; restart/load with delayed input. | Old PlayerId/generation/control token never regains authority, including queued actions. |
| COOP-005 | Host/guest edits own/AI/other-human soldier; bulk target set includes a forbidden target. | Exact permission table; forbidden bulk operation changes nothing. |
| COOP-005 | Two clients take the last item at one revision; displaced item/backpack/ammo conversion; auto-equip. | One commit; stale peer refreshes; all stock/loadout mutations atomic and conserved. |
| COOP-005/006 | Same ID replay, different-content replay, cache-evicted ID, reordered requests, previous preparation generation. | No mutation repeated; deterministic conflict/expiry/resync; bounded memory. |
| COOP-005 | Edit races claim/disconnect/freeze; readiness toggles versus equipment/presence changes; required item removed. | Commit-time rights, no double stock return, readiness tied only to committed preparation revision; deploy blocked until essentials repaired. |
| COOP-006 | Unmodded peer, incompatible major/minor/features/content; malformed/truncated/oversized frames and input before admission. | Bounded explicit rejection; no state mutation, spawn or input handler before admission. |
| COOP-006/007 | Out-of-order/gapped deltas, partial snapshot, obsolete result, screen close/reopen with edit in flight. | No mixed-revision view/rollback; resync disables edits; old screen is not called. |
| COOP-007 | Four slots/restricted mission, host without soldier, scripted/AI/human owners, pending/rejected requests, Loading/Frozen/Disconnected. | Fake-model visual/input evidence; local selection never mutates ownership; read-only state is clear. |
| COOP-008/009 | Dormant adapters in solo/native co-op; campaign actor ID differs from player slot; unowned AI takes/deals damage; driver changes owner. | No legacy regression, invalid indexing, class overwrite or obsolete input. Focused harness/build evidence; full vehicle acceptance remains later. |
| COOP-009/010 | Manifest disagreement, failed reconstruction, missing/duplicate/wrong-generation snapshot ack; peer leaves barrier. | No early simulation/input; explicit abort/re-evaluation; one generation and manifest on both machines. |
| COOP-010 | Two real instances, two humans/two AI, move/fire/reload/inventory/objectives; guest disconnects. | Same original mission/squad state; original soldier returns to host AI with health/gear/position/mines retained; host/client logs identify generation and control revision. |

Later save/load, live preparation synchronization, campaign interaction/completion and
late-join tasks extend this matrix; their runtime gates are not waived by these
foundation components. Log session/generation/request, soldier and control revision,
and accepted/rejected result for diagnosis without logging credentials.

## Open policies

The model represents capabilities and accepts explicit policy decisions. It returns
`PolicyRequired` and a deploy blocker when a required decision is absent; it must not
silently choose a new product rule. Fake/development fixtures may name a proposed
policy and clearly label it as such.

| Unconfirmed product decision | Independent work that can proceed | Gate that needs confirmation |
| --- | --- | --- |
| Host selects/assigns roster versus guests choosing eligible deployed soldiers; living-soldier switching/spectating details. | Catalog, atomic claim/switch operations, permission and stale-message tests with injected eligibility policy. | Production soldier selection, forced assignment and death/switch presentation. |
| Ready quorum and leave/cancel/resume behavior. | Revision-bound ready values, atomic freeze and barrier acknowledgments. | Shipping deploy policy/control; no "all ready" rule silently assumed. |
| Scarce stock UX, reservations and soldier-to-soldier requests. | Atomic first-committed transaction plus explicit stale/stock rejection; read-only coordination view. | Reservations or cross-soldier transfer feature. |
| Equipment editing after deployment or only existing campaign equipment steps. | Immutable deployment manifest and preparation-only edit API. | Any in-mission equipment menu or supply mutation outside original gameplay. |
| Host-owned saves/progress, shared pause/load, host-loss termination and both peers using the mod (proposed in design). | Host simulation authority, separate content negotiation, generation-safe reconstruction hooks. | Shipping persistence/pause/resume/host-loss policy. Protocol rejects incompatible peers whenever this campaign extension is selected; no vanilla support is claimed. |

No open decision blocks this documentation foundation or the isolated model/codec
tests. A production feature needing one must obtain and record that decision in the
design before connecting it. The first playable development test records its fixed
roster, loadouts and policy fixture instead of treating them as approved menu behavior.

## Contract review

Document review checks identity scopes against slot reuse; traces ownership, equipment
and freeze races; confirms every legacy finding has a named owner; and verifies the
fake UI can represent all required states without writable campaign globals. The task
[handoff](tasks/COOP-002.md#handoff) records actual review/check results and COOP-003
consumer feedback. This document contains no compiled declarations, so no target-header
compile or gameplay claim is made. New downstream headers still require their intended
MSVC x86 compile and appropriate component tests before their tasks pass review.
