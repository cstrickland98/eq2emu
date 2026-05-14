# Phase 1 Threading Inventory

Status: complete.

## Known Thread Creation Sites

| File | Line | Current behavior | Initial source2 owner candidate |
| --- | ---: | --- | --- |
| `source/WorldServer/client.h` | 232 | Detached `std::thread` for delayed client key/state work | Session executor or timer service |
| `source/LoginServer/LWorld.cpp` | 824 | `ServerUpdateLoop` thread | Login/world registration executor |
| `source/common/database.cpp` | 416 | Detached async DB query thread | DB worker pool |
| `source/common/database.cpp` | 459 | Detached async DB query thread | DB worker pool |
| `source/WorldServer/net.cpp` | 265 | Detached item load thread | Startup loader/task group |
| `source/WorldServer/net.cpp` | 268 | Detached spell load thread | Startup loader/task group |
| `source/WorldServer/net.cpp` | 273 | Detached Discord startup thread | Optional integration service |
| `source/WorldServer/net.cpp` | 383 | Detached peer poll thread | Peer/web service executor |
| `source/WorldServer/net.cpp` | 493 | Detached auto login server init thread | World/login connector service |
| `source/WorldServer/zoneserver.cpp` | 404 | Zone loop thread | Zone executor |
| `source/WorldServer/zoneserver.cpp` | 406 | Spawn loop thread | Zone executor or zone subsystem task |
| `source/WorldServer/zoneserver.cpp` | 415 | `pthread_cancel` zone thread shutdown | Explicit `std::jthread` stop |
| `source/WorldServer/zoneserver.cpp` | 416 | `pthread_cancel` spawn thread shutdown | Explicit `std::jthread` stop |
| `source/WorldServer/zoneserver.cpp` | 4919 | Detached level-change spawn send thread | Zone/client update queue |
| `source/WorldServer/zoneserver.cpp` | 5892 | Detached initial spawn send thread | Zone/client update queue |
| `source/common/EQStreamFactory.cpp` | 148 | Reader loop thread | Net transport |
| `source/common/EQStreamFactory.cpp` | 149 | Writer loop thread | Net transport |
| `source/common/EQStreamFactory.cpp` | 150 | Packet combine loop thread | Protocol/net boundary |
| `source/common/Log.cpp` | 293 | Detached log loop | Logging service |
| `source/common/TCPConnection.cpp` | 529 | Detached TCP connection loop | Net transport |
| `source/common/TCPConnection.cpp` | 561 | Detached TCP connection loop | Net transport |
| `source/common/TCPConnection.cpp` | 610 | Detached TCP connection loop | Net transport |
| `source/common/TCPConnection.cpp` | 1469 | Detached TCP server loop | Net transport |
| `source/WorldServer/Zone/map.cpp` | 293 | Detached async map load thread | Zone asset loader |
| `source/common/Web/WebServer.cpp` | 142 | Detached web server thread | Web service executor |
| `source/common/Web/WebServer.cpp` | 171 | Detached HTTPS session thread | Web service executor/thread pool |
| `source/common/Web/WebServer.cpp` | 174 | Detached HTTP session thread | Web service executor/thread pool |
| `source/WorldServer/Web/HTTPSClient.cpp` | 99 | `std::thread` for ASIO context | Web client service |
| `source/WorldServer/Web/HTTPSClientPool.cpp` | 392 | Worker thread collection | Web client worker pool |

## Risks

- Detached threads make shutdown ordering difficult to reason about.
- `pthread_cancel` can interrupt code while locks or resources are held.
- Network, DB, Lua, and zone work may call across subsystem boundaries directly.
- Lock ownership is distributed across domain objects rather than owned by execution contexts.
- `EQStreamFactory` locks stream maps while routing packets and timeout cleanup.
- Zone processing is split between zone loop and spawn loop, so zone-owned mutable state is not single-owner today.
- Client login can be delayed by async DB state, meaning login/session state crosses DB and zone concerns.
- Lua script state is protected by per-script mutexes, but script APIs can mutate gameplay objects directly.

## Source2 Ownership Targets

| Mutable state group | Proposed source2 owner |
| --- | --- |
| Login state | Login executor |
| World registration/session state | World executor |
| Zone state and spawn containers | Zone executor |
| Socket byte IO | Net transport threads |
| Blocking DB queries | DB worker pool |
| Lua engine state | Scripting service, called through owner-aware APIs |
| Logging queue | Logging service |
| Web/peer polling state | Web/peer service executor |

## Lock And Shared-State Inventory

| Area | Current lock/state pattern | Source2 implication |
| --- | --- | --- |
| Stream state | `EQStream` contains many `Mutex` members for state, queues, ACKs, rate, compression, and packet combination | Move socket state into net-owned session objects and avoid exposing stream internals to gameplay |
| Stream factory | `EQStreamFactory` protects `Streams` and `NewStreams` with custom `Mutex` | Net transport should own stream maps and publish connection/session events |
| Zone state | `ZoneServer` contains many `Mutex`, `MutexList`, `MutexMap`, `shared_mutex`, and two active loops | Collapse mutation into a zone executor; use snapshots/messages for cross-thread reads |
| Client state | `Client` mixes custom locks, standard mutexes, timers, packet queues, and gameplay state | Split session transport from player/gameplay state |
| World state | `World` protects many master lists with custom mutexes and shared mutexes | Split immutable/static data caches from mutable world/session services |
| Lua state | `LuaInterface` owns maps of script names to `lua_State*`, per-script mutex maps, and stale userdata tracking | Scripting should own Lua states and expose typed script contexts |
| DB async | `Database` owns async query vectors/maps and starts detached DB worker threads | Replace with bounded DB worker pool and completion posting |
| Logging | `Log.cpp` starts detached log loop | Replace with RAII logging service and controlled shutdown |

## Required Source2 Threading Decisions

- Use `std::jthread` and `std::stop_token` for owned worker threads.
- Remove `pthread_cancel` from zone shutdown semantics.
- Treat network IO, DB workers, web workers, and zone simulation as separate execution domains.
- Forbid direct mutation of zone state from network, DB, web, and Lua threads.
- Prefer messages/tasks over shared mutable containers.
- Keep lock scope small and do not hold locks while calling Lua, DB, or network writes.

## Remaining Review Questions

- Should source2 use one zone executor per loaded zone, or a fixed pool where each zone is pinned to an executor?
- Should login and world share any process-local services, or communicate only through protocol boundaries?
- Should web/peer APIs post into world/zone executors, or should they be modeled as separate command sources?
- Which static data can become immutable after startup and avoid locks entirely?

## Phase 1 Threading Exit Result

Threading inventory is sufficient for phase 2 planning. The central source2 correction is to replace object-level universal locking with executor ownership, especially for zones and client/player state.
