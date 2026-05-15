# Phase 4: Protocol Module

Status: complete.

## Purpose

Move packet and opcode behavior behind a dedicated protocol module with tests.

## Scope

Protocol owns packet structure, opcode mapping, stream framing rules that are protocol-specific, and version-specific serialization behavior.

## Deliverables

- Packet buffer reader/writer.
- Packet header types.
- Opcode manager abstraction.
- Versioned packet registration.
- Serialization tests using captured or golden packets.
- Compression/encryption boundary definitions.

## Progress

- Added protocol-owned packet buffer reader/writer helpers with bounds-checked reads and explicit wire byte order.
- Added protocol and application packet header helpers, including legacy one-byte login opcodes and padded two-byte opcode handling.
- Added application packet encode/decode helpers and transport transform boundary policy for compression, encoding, and CRC ownership.
- Added a versioned packet registry on top of opcode version ranges and opcode tables.
- Added protocol-owned combined-packet length framing helpers for legacy `OP_Combined` embedded packet payloads.
- Added raw packet CRC policy helpers that document the legacy session-packet and app-combined CRC bypasses without pulling CRC implementation into `net`.
- Added protocol-owned login/world interserver packet framing helpers for legacy TCP `ServerPacket` size, flags, opcode, compression metadata, destination metadata, and payload boundaries.
- Added protocol-owned `ServerLSInfo` payload decoding for fixed legacy login/world registration fields.
- Added standalone `eq2_protocol_tests` under the protocol module; it links only `eq2::protocol` and covers packet buffers, headers, session serialization, opcode lookup, versioned registration, and transform boundary behavior.
- Updated source2 login request parsing fixtures to consume the protocol packet reader/writer helpers instead of owning local byte-order routines.
- Updated source2 world-registration characterization to parse raw interserver LSInfo/keepalive bytes through protocol framing before applying login registration policy.
- Moved login-bound protocol characterization coverage under `source2/login/tests` so the protocol test directory remains dependency-clean.
- Confirmed the Debug source2 build and all current CTest tests pass after the second Phase 4 slice.
- Moved legacy login request packet parsing and login/world request-version fallback helpers into `eq2::protocol`, leaving `eq2::login` as a compatibility/policy wrapper.
- Added protocol-owned world-entry `LoginByNumRequest` parsing for the legacy and 1208 layouts, including the retry behavior described in the Phase 1 protocol inventory.
- Added protocol-owned `ServerOP_UsertoWorldReq` and `ServerOP_UsertoWorldResp` payload encode/decode helpers for the login-to-world character handoff boundary.
- Added shared protocol field helpers for EQ2 8-bit and 16-bit length-prefixed strings and moved login request parsing onto those helpers.
- Added protocol-owned `LS_PlayRequest` and `LS_PlayResponse` payload helpers for the client-facing play-character boundary.
- Added protocol-owned `LS_DeleteCharacterRequest` and `LS_DeleteCharacterResponse` payload helpers for the character-select delete boundary.
- Confirmed the Debug source2 build and all current CTest tests pass after the login request, world-entry request, character-handoff, play-character, and delete-character protocol slice.
- Audited the Phase 4 exit criteria: `eq2_protocol_tests` links only `eq2::protocol`, the protocol tree has no login, world, zone, database, scripting, or net dependencies, and login/world-facing characterization coverage consumes source2 protocol APIs rather than legacy packet internals.

## Exit Criteria

- Protocol tests run without login, world, zone, database, or scripting dependencies.
- Login and world code consume protocol APIs instead of legacy packet internals.
