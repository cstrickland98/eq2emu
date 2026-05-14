# Phase 5: Net Module

Status: planned.

## Purpose

Build source2 transport code without game rules.

## Scope

Network code owns sockets, sessions, byte movement, write queues, lifecycle events, and graceful shutdown. It must not know about accounts, characters, spawns, combat, quests, or Lua.

## Deliverables

- TCP server/session abstraction.
- UDP stream abstraction if needed for EQ2 protocol handling.
- Connection lifecycle events.
- Packet read/write dispatch boundary.
- Backpressure policy.
- Graceful stop behavior.

## Exit Criteria

- Net tests run with fake packet handlers.
- `eq2::net` depends only on infrastructure-level modules.
- No gameplay types appear in net headers.

