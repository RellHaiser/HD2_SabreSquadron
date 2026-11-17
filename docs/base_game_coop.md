# Base game cooperative campaign

## Goal and agreed behavior

Add **Co-op Campaign** to the singleplayer mode picker alongside Campaign, Lone Wolf,
and Carnage. Play the base game's campaign with its original squad, missions, scripts,
objectives, equipment, soldier progression, and mission sequence. Other players control
the squad's soldiers.

The agreed model is a campaign led by the host, with up to four players and AI controlling
unclaimed soldiers. Joining a session transfers control of an existing soldier. It does
not create another soldier or give the squad another life. A disconnect returns a living
soldier to AI control with the same health, inventory, position, and campaign identity.

Each human player selects their own soldier's loadout. The host selects their own loadout
and the loadouts of unclaimed soldiers controlled by AI. Hosting does not grant ordinary
editing access to another connected player's loadout.

Preparation needs a new UI that resembles the singleplayer campaign and supports multiple
players. Reuse the native networking and multiplayer infrastructure where useful, while
keeping the campaign experience free of the multiplayer loadout flow, scoreboards, and
round presentation. Reusing the multiplayer loadout UI is a possible fallback to discuss
if a concrete constraint requires it, rather than the default implementation.

**Status:** design and source investigation. The fork does not yet implement this mode.
The runnable port described in the root README is the upstream starting point.

## Proposed behavior for remaining decisions

These defaults guide the first prototype. The host-led campaign, four-player limit, AI
filling empty slots, loadout editing permissions, and campaign-style UI direction above
are agreed; the following details remain proposals.

- The host chooses the mission squad, and guests choose an available deployed soldier.
  Each player then equips their own soldier. Campaign missions that restrict the squad keep
  those restrictions; players without an eligible soldier spectate.
- The host runs the simulation and owns campaign progress and saves. Guests receive the
  campaign state needed for play without overwriting their own campaign progress.
- Soldier switching remains available for living, unclaimed squadmates. A player cannot
  take a soldier controlled by another player. A dead soldier stays dead under the
  campaign's rules; a player can take an available living squadmate or spectate.
- Host pause and tactical pause stop the shared simulation for everyone. Opening an
  ordinary guest menu does not pause the session.
- Mid-mission save and load restore the whole shared mission. Loading reconstructs the
  world and reassigns players to eligible soldiers before accepting input again.
- Losing the host ends the session. Host migration is outside the initial implementation.
- Both host and guests use this mod. The protocol must identify the mod and reject
  incompatible clients before campaign messages are exchanged.

## Existing code and required changes

| Area | Existing behavior | Required campaign co-op behavior |
| --- | --- | --- |
| [Mode picker](../src/game/main_menu.cpp), `SINGLE_MODES`, `OnSingleplayerButton` | Mode controls select campaign rules and clear the network session type. | Add the mode's control and assets, then host/join setup that preserves the campaign route. Audit the HXML/model resources used to build this screen. |
| [Campaign flow](../src/game/game_app.cpp), `StartStep`, `StartNextMap` | Campaign steps and multiplayer map changes use different routes; `StartNextMap` calls `SetupMultiplayerMission`. | Keep the campaign step sequence. Coordinate mission loading and transitions with all guests. |
| [Settings](../src/game/profile.cpp), `IsCooperativeGame`, `GetDifficulty` | Cooperative detection comes from multiplayer settings. Difficulty also branches on local/network play. | Select campaign difficulty and rules independently from the transport role. |
| [Mission lifecycle](../src/game/game.cpp), `C_game::Start`, `Stop` | Only a local game spawns the chosen campaign squad and writes soldier state back to the campaign at exit. | The host performs those campaign operations in a network session. Guests cannot independently commit campaign progress. |
| [Spawn points](../src/game/spawn_point.cpp), `Spawn`, `CreateSoldier` | Local spawns allocate squad actor slots; cooperative network spawns use multiplayer zones and bind a soldier to a player slot. | Spawn the selected campaign squad at the original campaign points, then bind controllers to those actors. |
| [Controllers](../src/game/human_control.cpp), `AssignPlayerController` | Network player control compares the human's net ID with the local player slot. Local play switches between player and squad controllers. | Resolve the controlling player independently of the soldier's identity; retain squad AI for unclaimed soldiers. |
| [Player input and stats](../src/game/player_net.cpp) | Helpers such as `StampActivity` and `ApplySoldierClass` index network players by the human's net ID. | Resolve the connection that controls the soldier. Campaign skills must not be replaced by multiplayer ranks/classes. |
| [Squad selection](../src/game/team.cpp), `SwitchTo`, `ReturnControl` | A single local active soldier can be swapped with another squad member. | Keep local camera selection separate from shared squad membership and host-authorized control transfers. |
| [Campaign equipment screen](../src/game/equipment_screen.cpp), `C_EquipmentMenu` | One local menu edits the mission squad and profile item stocks, including auto-equipping multiple soldiers. | Build a shared preparation model with per-soldier editing rights. Adapt campaign validation and equipment rules without letting a guest or host auto-equip another player's soldier. |
| [Equipment panel](../src/game/equipment_panel.cpp), `C_EquipmentPanel` | The panel selects and mutates one squad soldier, and renders the soldier and inventory controls. | Assess reuse of campaign visuals and inventory components behind the new UI; send validated edits through the shared preparation model instead of mutating replicated state directly. |
| [Script player team](../src/script/script_vm.cpp), `GetPlayerTeamForMode` | Native co-op scripts query `C_CoopTeam`; local scripts query `C_Team`. | Script queries must see the entire deployed campaign squad, including AI members. Audit other player and squad queries as well. |
| [AI replication](../src/game/ai_controller.cpp), `UpdateNetwork` | AI updates are sent for native cooperative network games. | Reuse the replication path with the host running AI. Verify that squad AI supplies all required controller state. |
| [Joining and leaving](../src/game/game_rounds.cpp), `OnPlayerJoined`, `OnPlayerLeft` | A join uses multiplayer spawning; a departure destroys the player's actor and mines. | Claim an existing eligible soldier on join. Release ownership on departure and preserve the soldier and deployed equipment. |
| [Saving](../src/game/game_save.cpp), [application saves](../src/game/game_app.cpp), [profile saves](../src/game/profile.cpp) | Mission snapshots and campaign records exist, but loading and restarting assume the original local/network split. | Add a host-controlled save/load transaction, client reconstruction, and separate co-op save discovery. |
| [HUD and input](../src/game/interface.cpp), [game menu](../src/game/game_menu.cpp) | Several squad, inventory, tactical-map, and switching features are gated on local play. | Restore campaign controls according to local ownership and host authority. |

The menu is only the entry point. Sending a campaign map through the current multiplayer
session flow would retain multiplayer spawning, rounds, disconnect behavior, and progression.

## Campaign preparation and loadouts

The new preparation screen should expose the selected squad, each soldier's human owner
or AI status, and the local player's equipment editor. The host can switch that editor
between their own soldier and unclaimed AI soldiers. Other players' loadouts may be shown
for coordination, but editing remains subject to ownership. Show the briefing and campaign
equipment information in the singleplayer style. A player list or readiness indicator
should serve preparation; it should not introduce multiplayer ranks or scoring.

Separate the shared preparation state from its controls and rendering. This lets the UI,
equipment rules, and transport adapters develop independently. The existing campaign
equipment panel, soldier preview, item definitions, weight rules, and required-equipment
checks are candidates for reuse. Their current direct changes to global squad and stock
objects need adaptation before simultaneous editors can use them safely.

### Proposed synchronization rules

Keep the host authoritative over the shared campaign stock and committed loadouts. Players
select equipment on their own clients; the host validates and commits those requests, so
host authority does not replace the player's choice. Requests identify the preparation
generation, soldier, editor, and expected state revision. Permission checks use the current
soldier owner, with the host permitted to edit their own and unclaimed soldiers only.

Apply each accepted edit as one transaction across the soldier's loadout and campaign
stock. Concurrent requests for the last available item must not duplicate it or overwrite
another edit. Return accepted state or a specific rejection and current state to the
editor, then replicate committed changes to the other participants. Reject duplicate and
stale requests without applying them again. Preserve the existing campaign limits on
equipment, weight, ammunition, and mandatory mission items.

Claiming or releasing a soldier updates editing rights and invalidates outstanding edits
from the previous owner. A joining player receives the existing loadout and can edit it
under the preparation rules; disconnecting must not reset equipment or return it to stock
twice. Auto-equip and other bulk operations use the same permission and transaction path
as individual item edits.

Readiness should refer to a committed preparation revision. A relevant loadout or squad
change invalidates readiness before deployment. Once deployment begins, freeze preparation
edits and send the same committed squad state to every client. Ordinary in-mission item
transfers remain a separate gameplay path and need their own ownership and authority audit.

### Product decisions still open

- Whether players choose roster soldiers themselves or the host assigns the squad first.
- Whether deployment requires every connected player to be ready, and how a host cancels
  or resumes preparation when someone leaves.
- How the UI resolves competition for scarce shared equipment, including whether to add
  reservations or allow players to request transfers from another soldier.
- Whether players may change loadouts after deployment or only at the campaign's existing
  equipment steps; this must not create equipment unavailable in ordinary singleplayer.

## Separate campaign rules from networking

Retain the meaning of `IsLocalGame()`, `IsServer()`, and `IsClient()`. The host needs a real
network session, and guests must not acquire server authority. Marking campaign co-op as
local globally would bypass network updates and allow the wrong simulation paths to run.

Introduce a session context that distinguishes campaign rules, transport role, and local
player ownership. Ordinary singleplayer, existing multiplayer, and the new cooperative
campaign must remain distinguishable. Keep `PLAY_MODE_CAMPAIGN` as the underlying campaign
rules where possible, with an explicit cooperative session kind. A fourth menu entry need
not become an unchecked fourth value in every legacy play-mode array.

Audit each relevant local/network branch by purpose: simulation authority, campaign rules,
or presentation for the local player. Convert branches deliberately. Existing native co-op
checks also need review: some enable useful replication, while others select multiplayer
lives, scoring, or team semantics.

Co-op saves need their own namespace and resume entry, including quick saves and the
profile's last-save tracking. Keep the host's existing solo campaign saves separate.

## Soldier identity and control

Maintain an explicit host-owned mapping between a connection/player slot and a deployed
campaign soldier. The soldier's roster identity persists through campaign progression;
its actor and replication IDs identify its current incarnation in a mission. Rebuild that
mapping after a load or mission change. Never persist process pointers or treat a temporary
connection slot as a campaign identity.

Enforce these invariants at every control transfer:

1. Each deployed soldier has at most one human owner, and each player controls at most
   one soldier at a time.
2. Unowned eligible soldiers run squad AI on the host. Guests display replicated state
   for soldiers they do not control.
3. Claims target living, available soldiers in the current mission. The host validates
   requests; clients cannot assign themselves a controller by sending an actor ID.
4. A transfer changes control without respawning, resetting skills, replenishing gear,
   reviving, or clearing persistent mission actions.
5. Scripted control can temporarily override player control. Releasing scripted control
   restores the appropriate owner or AI controller.
6. A load, restart, or mission transition invalidates prior input and ownership messages.
   Use a mission generation and control revision so delayed packets cannot control a
   newly created or reassigned actor.

Controller handoff must update both replication and local presentation: the input source,
network object handlers and queued actions, camera target, first-person weapon view,
inventory target, and squad selection. Reusing `CreateController` alone does not establish
that these responsibilities have been transferred correctly.

## Mission synchronization

The host starts the original campaign mission and supplies its mission identity, difficulty,
selected soldiers, and equipment to guests. A loading barrier keeps the simulation stopped
until participating clients have reconstructed the initial state and acknowledged readiness.

The host evaluates authoritative AI, damage, script conditions, objectives, and campaign
results. Audit the script module's existing network behavior before reusing it: presentation
commands, camera sequences, dialog, and scripted player control require appropriate execution
on each client without duplicating authoritative effects.

Initial mission entry can use the existing spawn/state mechanisms after adapting campaign
spawning. Joining an already running mission additionally needs a complete current-world
snapshot, including script progress, dynamic actors, objectives, inventory, and vehicles.
Treat that as a separate acceptance gate; replaying the original spawn message is insufficient.

Loading a save similarly stops input, advances the mission generation, reconstructs the
host world, resets replication, rebuilds guest worlds, restores eligible ownership, and
waits for acknowledgments before resuming. Connection membership remains live session
state rather than being blindly restored from the save file.

## Implementation milestones

### 1 Campaign session and ownership foundation

Define the explicit session context and soldier ownership model, with tests for competing
claims, invalid soldiers, disconnects, switching, death, and stale requests. Map every path
that assumes actor ID equals player slot, including damage attribution and inventory
requests. Establish a reproducible x86 build and baseline singleplayer/native co-op run.

**Acceptance:** ordinary play still follows its original routes, and ownership transitions
are tested before being connected to live controllers.

The preparation model and UI investigation can run alongside this foundation. Agree on
soldier identity, ownership revisions, and preparation state contracts before connecting
loadout transactions to the network. Deliver UI components and equipment transaction logic
as separate tested features so either can be reassigned without depending on an agent's
private context.

### 2 Two players in one original mission

Use a development launch path for an original base campaign mission with four deployed
soldiers. The host controls one soldier, one guest controls another, and the remaining two
use squad AI. Adapt campaign spawn, controller routing, initial state replication, player
team queries, and campaign mission ticking together. Campaign play must bypass multiplayer
warmup, round scoring, lives, respawns, and map rotation.

**Acceptance:** both players can move, shoot, reload, use inventory, and observe the same
AI squad and objective progress. Disconnecting the guest returns the same soldier to AI
with its state intact. This gate requires two running game instances.

### 3 Campaign interaction and mission completion

Validate squad orders, soldier switching, healing, item transfers, disguise, doors,
explosives, vehicles, scripted sequences, failure, and victory. Confirm soldier deaths and
stats persist in the host's campaign state. Keep all deployed soldiers visible to mission
queries regardless of human ownership.

**Acceptance:** the test mission completes through its original objectives, and the host
reaches the original debrief and following campaign step with the correct squad state.

### 4 Singleplayer menu and campaign sessions

Add the Co-op Campaign control and required menu resources, host/join setup, soldier claims,
and a four-player limit. Implement the new campaign-style preparation UI: each player
equips their own soldier, and the host equips their own and unclaimed AI soldiers. Connect
briefing, squad selection, validated loadout transactions, and subsequent missions to the
synchronized campaign flow. Handle missions with fewer available soldiers and leaving a
session at every campaign screen. Reuse campaign presentation components where practical;
do not make the multiplayer loadout screen or scoreboard part of the normal flow.

**Acceptance:** start and continue a base cooperative campaign entirely through the game's
menus, with one through four connected players and AI filling available empty slots. Two
players can edit their own loadouts concurrently without changing each other's soldiers
or duplicating shared stock. The host can equip AI soldiers, and deployment uses the
same committed loadouts on every machine.

### 5 Persistence and joining during play

Implement isolated campaign saves, quick save/load, restart, reconnect, and a current-world
snapshot for joining during a mission. Exercise loading after deaths, equipment changes,
vehicle use, objective changes, and scripted events.

**Acceptance:** connected players converge after loading and reconnecting, and solo saves
remain intact. A new connection cannot gain control before reconstructing the current world.

### 6 Base campaign verification

Track each base mission through completion with the relevant player counts and special
sequences. Compare campaign outcomes and soldier state with ordinary singleplayer. Include
missions with constrained squads, transitions, scripted control, and vehicle sequences.

**Acceptance:** campaign behavior is demonstrated mission by mission. Loading every map
alone does not establish campaign compatibility.

## Validation requirements

The upstream [build instructions](../README.md#building) use MSVC x86 and x87 floating-point
behavior for the runnable executable. `_re/tools/build_check.py` provides a fallback g++
syntax check, which does not replace the runnable build or multiplayer verification.

Use focused native tests for ownership and session logic, existing subsystem harnesses for
changed legacy paths, and actual host/guest play for controller handoff, replication, and
mission behavior. Test loadout permissions, simultaneous requests for scarce equipment,
stale and repeated edits, ownership changes during editing, and readiness invalidation.
Keep a small regression set for original singleplayer and native co-op.
Record host and client logs with mission generation, soldier identity, and control revision
so divergence can be traced to a specific handoff or transition.

Playable validation requires a local game installation with Sabre Squadron 1.12 and the
matching port engine libraries. The source repository supplies neither the game's data
nor evidence that arbitrary base missions already synchronize correctly in native co-op.
