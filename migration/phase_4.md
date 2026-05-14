# Phase 4: Protocol Module

Status: planned.

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

## Exit Criteria

- Protocol tests run without login, world, zone, database, or scripting dependencies.
- Login and world code consume protocol APIs instead of legacy packet internals.

