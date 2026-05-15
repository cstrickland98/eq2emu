# Phase 5: Net Module

Status: complete.

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

## Progress

- Added `eq2::net::Session`, `SessionId`, transport kind, lifecycle event, and event sink abstractions.
- Added a bounded `WriteQueue` and `BackpressurePolicy` so packet writers can be rejected before unbounded memory growth.
- Added `eq2::net::TcpServer` as a source2 TCP server/session abstraction with accept, receive, send, drain, disconnect, and stop boundaries.
- Added `eq2::net::UdpStreamServer` as the UDP stream abstraction needed for EQ2 client protocol handling.
- Kept `eq2::net` dependent only on `eq2::core` and the thread dependency; net headers do not include protocol, login, world, zone, database, scripting, or gameplay types.
- Added `eq2_net_tests` with fake packet handlers covering lifecycle events, inbound packet dispatch, TCP sessions, UDP stream datagrams, FIFO write draining, backpressure rejection, and graceful stop behavior.

## Exit Criteria

- Net tests run with fake packet handlers.
- `eq2::net` depends only on infrastructure-level modules.
- No gameplay types appear in net headers.
