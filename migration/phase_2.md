# Phase 2: Characterization Tests

Status: complete.

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
- Added login authentication success, invalid password, bad version, account creation, and duplicate-session characterization coverage.
- Added world registration admission coverage for unauthenticated packet rejection, valid `ServerOP_LSInfo`, bad versions, invalid world accounts, disabled accounts, and debug world type handling.
- Added legacy login request field parsing coverage for length-prefixed credentials, client version, and truncated packet rejection.
- Added character select mapping coverage for `login_characters`, `login_char_colors`, and `login_equipment` behavior, including legacy SOGA fallback fields, packet version constants, appearance color/signed values, and the twenty-four-row equipment cap.
- Added fake repository character-list coverage that verifies account-id loading and per-character appearance/equipment lookup by `login_characters.id`.
- Golden packet coverage is represented by inline byte fixtures for known protocol/session/login request layouts; no captured external packet fixture files are currently available in the repository.

## Exit Criteria

- Tests can run without starting the full legacy server.
- Tests document expected legacy behavior.
- Source2 protocol and login work can be validated against these tests.
