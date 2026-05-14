# Phase 9: Zone Runtime

Status: planned.

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

## Exit Criteria

- Zone state is mutated only by the zone owner executor.
- External systems post commands or consume snapshots.
- No arbitrary-thread mutation of spawn containers remains in source2 zone code.

