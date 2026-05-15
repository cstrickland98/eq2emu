# Phase 9: Zone Runtime

Status: complete.

## Purpose

Create a source2 zone runtime with explicit ownership of mutable zone state.

## Scope

Zone owns spawn state, movement, local timers, combat events, and client update fanout for a zone or zone group.

## Deliverables

- `ZoneRuntime`.
- Zone command queue.
- Zone tick loop.
- Spawn ownership model.
- Movement update boundary.
- Combat event boundary.
- Client subscription/update boundary.

## Progress

- Added `eq2::zone::ZoneRuntime` with an explicit zone owner tick boundary.
- Added `ZoneCommand` and a queue for add-spawn, move-spawn, remove-spawn, combat-event, subscribe, and unsubscribe commands.
- Added `SpawnState`, `Position`, `CombatEvent`, `ZoneUpdate`, `ClientId`, and immutable `ZoneSnapshot` read models.
- Zone state is mutated only while `ZoneRuntime::tick()` drains commands on the owner thread; external systems post commands or read snapshots.
- Added movement and combat boundaries as commands that are resolved during the owner tick.
- Added client subscription/update fanout without exposing mutable spawn containers.
- Added `eq2_zone_runtime_tests` covering owner-tick mutation, movement, combat, subscription fanout, and cross-thread command posting.

## Exit Criteria

- Zone state is mutated only by the zone owner executor.
- External systems post commands or consume snapshots.
- No arbitrary-thread mutation of spawn containers remains in source2 zone code.
