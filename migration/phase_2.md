# Phase 2: Characterization Tests

Status: in progress.

## Purpose

Capture current behavior before replacing legacy code. These tests should preserve known behavior, including quirks, so source2 can be compared against the current server.

## Scope

Add focused tests around protocol, login, world registration, character select, database mapping, and any small deterministic gameplay logic that can be isolated.

## Deliverables

- Packet header encode/decode tests.
- Opcode lookup tests by client version.
- Login packet parsing tests.
- Login authentication success/failure tests.
- World server registration flow tests.
- Character list mapping tests.
- Database repository tests using a fake query layer or isolated test database.
- Golden packet fixtures where available.

## Progress

- Added CTest wiring for source2 characterization tests.
- Added protocol/session encode and decode characterization coverage for legacy session request and response wire fields.
- Added protocol opcode prefix characterization for legacy one-byte protocol opcodes.
- Added opcode version range lookup coverage matching legacy `GetOpcodeVersion` behavior.
- Added opcode table coverage for missing opcode sentinel and replacement behavior.
- Added login/world request struct version fallback coverage for the Phase 1 login and world entry flows.

## Exit Criteria

- Tests can run without starting the full legacy server.
- Tests document expected legacy behavior.
- Source2 protocol and login work can be validated against these tests.
