# EQ2Emu Source2 Migration Plan

This document describes the planned migration from the current `source` tree to a new `source2` architecture. The goal is not to rewrite EQ2Emu for its own sake. The goal is to preserve working emulator behavior while introducing clear ownership boundaries, predictable threading, and a maintainable build/dependency graph.

## Current State

The current codebase is functional but tightly coupled. The existing tree is organized around:

- `source/common`
- `source/LoginServer`
- `source/WorldServer`

The legacy build is makefile based. Current Linux makefiles pull in dependencies such as MariaDB, zlib, Boost, OpenSSL/Crypto, Lua, Recast/Detour, readline, glm, fmt include paths, and pthreads.

The current runtime also contains several architectural risks that should not be carried forward unchanged:

- Detached `pthread` and `std::thread` usage.
- Thread cancellation patterns.
- Shared mutable state protected by scattered mutexes.
- Common code that mixes protocol, networking, database, and server behavior.
- World and zone logic that directly reaches across subsystem boundaries.
- Lua integration that can call deeply into game state.

## Migration Strategy

Use a new `source2` tree inside this repository. Keep the legacy `source` tree buildable while new code is migrated in vertical slices.

Do not start a separate repository unless the current repository becomes unusable. A separate repo makes compatibility testing, database updates, script migration, and bug fixes harder to coordinate.

The migration should be incremental:

1. Define the new architecture and dependency graph.
2. Add a target-based CMake scaffold.
3. Move behavior behind tests and narrow interfaces.
4. Rebuild one vertical slice at a time.
5. Retire legacy code only after its replacement is verified.

## Source2 Module Layout

The planned module tree is:

```text
source2/
  core/
  protocol/
  net/
  db/
  login/
  world/
  zone/
  scripting/
  apps/
    login_server/
    world_server/
  tools/
```

### Module Responsibilities

`core`

- Logging abstractions.
- Configuration loading.
- Time and timer primitives.
- IDs, result/error types, byte buffers, utility types.
- Threading primitives shared by infrastructure.
- No dependency on game, protocol, database, networking, or Lua code.

`protocol`

- EQ2 opcodes.
- Packet headers.
- Serialization and deserialization.
- Version-specific packet layouts.
- Compression/encryption boundaries where they are part of packet framing.
- No dependency on login, world, zone, database, or scripting logic.

`net`

- TCP and UDP transport.
- Session lifetime.
- Read/write loops.
- Packet dispatch hooks.
- Backpressure and connection shutdown policy.
- No game rules.

`db`

- Database connection configuration.
- Connection pool.
- Query execution primitives.
- Repository interfaces and implementations.
- Migration helpers or schema version checks if added later.
- No direct dependency on server executables.

`login`

- Account authentication.
- Login server state.
- World server registration and status.
- Login-specific packet handlers.
- Depends on `core`, `protocol`, `net`, and `db`.

`world`

- Client session coordination after login.
- Character list, character select, zoning handoff, and world-level services.
- Owns high-level world state but should not directly mutate zone internals from arbitrary threads.
- Depends on `core`, `protocol`, `net`, `db`, `zone`, and `scripting`.

`zone`

- Zone runtime.
- Spawn state.
- Movement, combat, local timers, local events.
- Zone-owned command queue.
- Depends on `core`, `protocol`, `db`, and `scripting`.

`scripting`

- Lua engine lifetime.
- Script loading.
- Script call boundary.
- Script context APIs.
- Should expose controlled operations rather than raw access to world or zone internals.

`apps`

- Thin executable entry points.
- Parse config, wire dependencies, start runtime, handle shutdown.
- No business logic.

`tools`

- Developer utilities, packet inspection tools, migration helpers, and test harness support.

## Dependency Direction

The desired dependency direction is:

```text
apps/login_server -> login -> protocol, net, db, core
apps/world_server -> world -> zone, protocol, net, db, scripting, core
zone              -> protocol, db, scripting, core
protocol          -> core
net               -> core
db                -> core
scripting         -> core
core              -> nothing project-specific
```

Rules:

- Lower-level modules must not include headers from higher-level modules.
- `protocol` must never depend on `login`, `world`, or `zone`.
- `net` must not know account, character, spawn, or combat concepts.
- `db` should expose repositories or query services, not raw global database objects.
- `apps` should be composition roots only.

## Threading Model

The new architecture should be based on ownership rather than universal locking.

### Primary Rule

Mutable game state has one owner executor.

Examples:

- Login state is owned by a login executor.
- World/session state is owned by a world executor.
- Zone state is owned by a zone executor.
- Blocking database work is owned by a database worker pool.
- Network IO owns sockets and byte movement, not game state.

### Communication Rule

Threads communicate by messages, tasks, futures, or completion callbacks posted back to the owning executor.

Avoid:

- Detached threads.
- `pthread_cancel`.
- Calling into zone state from network or DB threads.
- Exposing shared mutable containers.
- Holding locks while invoking Lua, networking, or database operations.

Prefer:

- `std::jthread`.
- `std::stop_token`.
- RAII shutdown.
- Bounded queues for cross-thread commands.
- Clear executor ownership.
- Narrow immutable snapshots when data must cross boundaries.

## Build Plan

The source2 build uses root-level CMake and target-based dependencies. The initial CMake scaffold intentionally does not build the legacy `source` tree.

Initial commands:

```bash
cmake -S . -B build/source2
cmake --build build/source2
```

The first phase creates placeholder source2 targets and thin placeholder executables. Real dependencies such as MariaDB, Lua, Boost, OpenSSL, Recast, glm, and fmt should be added only when the corresponding module starts using them.

## Migration Phases

### Phase 0: Scaffold

Status: current phase.

- Add this migration plan.
- Create `source2` folder structure.
- Add root CMake entry point.
- Add source2 CMake targets.
- Keep legacy `source` untouched.

### Phase 1: Behavior Inventory

Document the current system before moving logic:

- Executables and ports.
- Login flow.
- World registration flow.
- Client connection lifecycle.
- Character select flow.
- Zone entry flow.
- Packet framing and opcode version handling.
- Database tables used by login, world, and zone.
- Lua script entry points and game-state mutations.
- Existing thread loops and shared-state ownership.

Deliverables:

- `docs/source2_runtime_inventory.md`
- `docs/source2_threading_inventory.md`
- `docs/source2_protocol_inventory.md`

### Phase 2: Characterization Tests

Add tests around current behavior before replacing it.

Priority tests:

- Packet header encode/decode.
- Opcode lookup by client version.
- Login packet parsing.
- Login authentication success/failure.
- World server registration packet flow.
- Character list query mapping.
- Basic DB repository behavior against a test database or fake query layer.

These tests should initially tolerate legacy quirks. The first purpose is to prevent accidental behavior loss.

### Phase 3: Core Module

Implement foundational source2 primitives:

- Logging interface.
- Config loader interface.
- Monotonic clock abstraction.
- Timer utilities.
- Byte buffer and endian helpers.
- Result/error type.
- Thread-safe queue or executor primitive.
- Graceful shutdown primitives.

Do not import large gameplay classes into `core`.

### Phase 4: Protocol Module

Move packet and opcode behavior behind protocol tests.

Deliverables:

- Packet buffer reader/writer.
- Packet header types.
- Opcode manager abstraction.
- Versioned packet registration.
- Serialization tests using captured/golden packets where available.

Acceptance criteria:

- Protocol tests run without login/world/zone dependencies.
- Login and world can consume protocol APIs without including legacy packet internals.

### Phase 5: Net Module

Build transport without game rules.

Deliverables:

- TCP server/session abstraction.
- UDP stream abstraction if needed for EQ2 protocol handling.
- Connection lifecycle events.
- Packet read/write dispatch boundary.
- Graceful stop.

Acceptance criteria:

- Net tests can run with fake packet handlers.
- No account, character, spawn, combat, or Lua types appear in `net`.

### Phase 6: DB Module

Create controlled database access.

Deliverables:

- Connection configuration.
- Connection pool.
- Query execution wrapper.
- Repository interfaces for login/account/world character data.
- Test double or fake DB implementation for unit tests.

Acceptance criteria:

- Login code does not call raw MariaDB APIs directly.
- DB work can execute off the owner executor and return results safely.

### Phase 7: Login Vertical Slice

Rebuild login first because it is smaller than world/zone and has clearer boundaries.

Deliverables:

- Login app wiring.
- Login server config.
- Account authentication through `db`.
- Login packet handling through `protocol`.
- Network transport through `net`.
- World registration handling.

Acceptance criteria:

- Existing client can reach the same login outcome as legacy login server.
- Legacy world server compatibility is either maintained or explicitly documented as temporarily unsupported.

### Phase 8: World Session Slice

Rebuild the first world flow:

- World server login/registration.
- Client session creation.
- Character list.
- Character select.
- Initial zone handoff boundary.

Acceptance criteria:

- Login-to-world flow is functional.
- Character list behavior matches legacy behavior for known accounts.

### Phase 9: Zone Runtime

Design and migrate zone simulation deliberately.

Deliverables:

- `ZoneRuntime`.
- Zone command queue.
- Zone tick loop.
- Spawn ownership model.
- Movement update boundary.
- Combat event boundary.
- Client subscription/update boundary.

Acceptance criteria:

- Zone state is mutated only by the zone owner executor.
- External systems post commands or consume snapshots.
- No direct arbitrary-thread mutation of spawn containers.

### Phase 10: Lua Boundary

Wrap scripting behind a narrow API.

Deliverables:

- Script engine lifetime management.
- Script context object.
- Explicit script-call APIs.
- Controlled operations that post back to the owning executor when mutation is required.

Acceptance criteria:

- Lua calls do not require holding zone/world locks.
- Script APIs are documented and testable.
- Script failures are isolated and reported.

### Phase 11: Gameplay Systems

Migrate gameplay systems in small vertical groups:

- Spawn lifecycle.
- Inventory.
- Items.
- Quests.
- Spells.
- Combat.
- Tradeskills.
- Guilds.
- Broker.
- Housing.
- Achievements and collections.

Each migration should include:

- Current behavior notes.
- Minimal tests.
- Interface placement.
- Thread ownership decision.
- Legacy compatibility notes.

### Phase 12: Legacy Retirement

After source2 reaches functional parity for a subsystem:

- Stop adding new features to the legacy subsystem.
- Keep bug fixes minimal and mirrored if needed.
- Remove legacy code only after source2 has tests and live smoke coverage.
- Update deployment docs and build scripts.

## Review Gates

Pause after each gate:

1. Source2 scaffold exists and CMake configures.
2. Current runtime/threading inventory is reviewed.
3. Protocol tests pass.
4. Net transport tests pass.
5. Login vertical slice works.
6. World session slice works.
7. Zone runtime ownership model is reviewed.
8. Lua boundary is reviewed.
9. First gameplay system migration is complete.

## Non-Goals For The First Pass

- Do not port all legacy code immediately.
- Do not redesign the database schema during initial scaffolding.
- Do not rewrite gameplay behavior without characterization tests.
- Do not make CMake responsible for the legacy makefile build yet.
- Do not introduce a large framework before the module boundaries are proven.

## Immediate Next Step After This Scaffold

Create the behavior inventory documents and map current thread ownership. The migration should not proceed into protocol or login code until the current login/world/zone flows are written down in enough detail to compare old and new behavior.
