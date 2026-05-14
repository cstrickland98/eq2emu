# Phase 1: Behavior Inventory

Status: active.

## Purpose

Document how the current server behaves before migrating logic into source2. This phase prevents the rewrite from becoming guesswork. The output should describe the existing runtime, thread ownership, protocol flow, database access, and Lua interaction clearly enough that source2 behavior can be compared against legacy behavior.

## Scope

Inventory the current `source` tree. Do not port code yet.

Primary areas:

- Login server executable behavior.
- World server executable behavior.
- Client connection lifecycle.
- Login-to-world registration flow.
- Character list and character select flow.
- Zone entry and zone handoff flow.
- Current thread loops and detached worker patterns.
- Shared mutable state and lock ownership.
- Database tables and query responsibilities.
- Lua script entry points and game-state mutation points.
- Packet framing, opcode lookup, and version handling.

## Deliverables

Create these documents:

- `migration/phase_1_runtime_inventory.md`
- `migration/phase_1_threading_inventory.md`
- `migration/phase_1_protocol_inventory.md`
- `migration/phase_1_database_inventory.md`
- `migration/phase_1_lua_inventory.md`

Each inventory should include:

- Files/classes/functions involved.
- Current ownership assumptions.
- Inputs and outputs.
- Known side effects.
- Risk notes.
- Source2 migration implications.

## Investigation Checklist

### Runtime

- Identify every executable entry point.
- Identify config files consumed by each executable.
- Identify server ports and connection types.
- Trace startup order.
- Trace shutdown behavior.
- Identify global singletons or global state.
- Identify cross-server communication between login and world.

### Threading

- List all explicit thread creation sites.
- Categorize each thread as network IO, DB work, zone loop, spawn loop, web server, script work, or miscellaneous.
- Identify detached threads.
- Identify cancellation or unsafe shutdown patterns.
- Identify shared containers protected by custom `Mutex`, `MutexMap`, `MutexList`, or standard mutexes.
- Identify calls that hold locks while calling DB, network, or Lua code.
- Propose the source2 owner executor for each major mutable state group.

### Protocol

- Identify packet header structures.
- Identify opcode tables and version-specific behavior.
- Identify packet encode/decode helpers.
- Identify login packet flow.
- Identify world packet flow.
- Identify zone entry packet flow.
- Identify encryption, compression, CRC, and stream-framing responsibilities.

### Database

- Identify login database access points.
- Identify world database access points.
- Group queries by feature area.
- Record tables used during login, world registration, character select, and zone entry.
- Identify synchronous queries on hot paths.
- Identify async query mechanisms.
- Propose source2 repository boundaries.

### Lua

- Identify script engine lifetime.
- Identify script loading and reload paths.
- Identify every exposed Lua API category.
- Identify script calls from client, world, zone, item, quest, spell, spawn, and region behavior.
- Identify whether script calls occur under locks.
- Propose source2 script context boundaries.

## Output Format

Use concise tables where useful:

```text
Area | Current files | Current owner | Source2 owner | Risk
```

For flows, prefer numbered event traces:

```text
1. Listener accepts connection.
2. Session reads packet.
3. Packet is decoded.
4. Handler performs DB lookup.
5. Response packet is emitted.
```

## Exit Criteria

- Runtime inventory is complete enough to explain startup, login, world registration, character select, and zone entry.
- Threading inventory lists all known thread creation sites and assigns a proposed source2 owner model.
- Protocol inventory identifies packet framing, opcode, version, compression, encryption, and stream responsibilities.
- Database inventory identifies critical login/world/zone tables and query ownership.
- Lua inventory identifies script entry points and mutation boundaries.
- Risks are captured before phase 2 test planning begins.

## Review Gate

Pause after the inventory documents are written. Review whether the proposed source2 ownership boundaries are correct before writing characterization tests.

