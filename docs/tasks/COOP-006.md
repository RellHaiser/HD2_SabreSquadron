# COOP 006 Protocol codecs and compatibility

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-006-protocol`
- Base commit: not started
- Dependencies: COOP-002, COOP-011 (merged shared declarations)
- Last checkpoint: none
- Checkpoint location: none
- Review/integration: none
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

Implement the agreed campaign protocol codecs and compatibility negotiation component
over the existing transport interfaces. Provide an independently testable adapter boundary;
live campaign session attachment and world synchronization remain later integration work.

## Files and shared contracts

Own new protocol/codec files and network harness additions at paths agreed in COOP-002.
Inspect `src/net/bitstream.*`, `net_game.*`, `net_commands.cpp`, and message dispatch before
choosing an extension point. Serialize edits to existing dispatch files with COOP-008/009.

### COOP-002 scope refinement

Contract implementation scope from COOP-002: own `src/net/campaign_protocol.{h,cpp}`
and `_re/harness/campaign_protocol/`. Follow [the negotiation and snapshot contract](../coop_contracts.md#protocol-compatibility-and-snapshot-boundaries).
Specify version, command reservation after dispatch audit, framing, byte/bit order,
limits, content descriptors and deterministic fixtures. Keep `S_JoinRequest` and native
message layouts unchanged. Public codec binding uses merged COOP-011 DTOs.
Deliver the concrete admission/quarantine and actor-input envelope attachment plan;
COOP-010 installs live `net_game.cpp`/`net_commands.cpp`/`net_messages.cpp` hooks after
adapters merge. Tests must reject pre-admission mutation without claiming a live handshake.

## Acceptance criteria

- Encode/decode the agreed identities, revisions, requests, acknowledgments, and snapshots
  without process pointers or unchecked assumptions about legacy IDs and bit widths.
- The negotiation component accepts compatible peers and rejects unsupported campaign
  versions before processing campaign payloads; test this sequencing and malformed input.
- Verify repeated/stale messages, bounds, and deterministic round trips using fixtures.
- Ordinary multiplayer message handling remains compatible and does not inherit campaign
  UI/scoring semantics. No incompatible packet is emitted in an ordinary session.
- Document the exact live attachment point and its later test gate; codec tests alone do
  not establish that a real host/client handshake or world synchronization works.

## Handoff

- Completed: task scope only; native transport available for investigation.
- Remaining: extension audit, codecs, compatibility component, fixtures, adapter documentation.
- Next action: read the merged COOP-002 protocol contract and trace dispatch/bitstream bounds.
- Blockers: COOP-002 and COOP-011 have not merged.
- Decisions: reuse networking infrastructure; keep campaign semantics explicit.
- Validation: none for this task.
