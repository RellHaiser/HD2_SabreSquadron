# COOP 006 Protocol codecs and compatibility

## Coordination

- Status: planned
- Owner/session: unassigned
- Canonical branch: `feature/coop-006-protocol`
- Base commit: not started
- Dependencies: COOP-002
- Last checkpoint: none
- Checkpoint location: none
- Review/integration: none

## Result and scope

Implement the agreed campaign protocol codecs and compatibility negotiation component
over the existing transport interfaces. Provide an independently testable adapter boundary;
live campaign session attachment and world synchronization remain later integration work.

## Files and shared contracts

Own new protocol/codec files and network harness additions at paths agreed in COOP-002.
Inspect `src/net/bitstream.*`, `net_game.*`, `net_commands.cpp`, and message dispatch before
choosing an extension point. Serialize edits to existing dispatch files with COOP-008/009.

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
- Blockers: COOP-002 has not merged.
- Decisions: reuse networking infrastructure; keep campaign semantics explicit.
- Validation: none for this task.
