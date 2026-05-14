# Phase 4: Protocol Module

Status: in progress.

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

## Exit Criteria

- Protocol tests run without login, world, zone, database, or scripting dependencies.
- Login and world code consume protocol APIs instead of legacy packet internals.
