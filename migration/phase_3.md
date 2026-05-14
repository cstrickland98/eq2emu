# Phase 3: Core Module

Status: complete.

## Purpose

Implement source2 foundational primitives that are safe to share across all modules.

## Scope

The `core` module should remain project-infrastructure focused. It must not absorb gameplay, networking, database, protocol, or Lua behavior.

## Deliverables

- Logging interface.
- Configuration interface.
- Monotonic clock abstraction.
- Timer utilities.
- Byte buffer and endian helpers where they do not belong more specifically in `protocol`.
- Result/error type.
- Thread-safe queue or executor primitive.
- Graceful shutdown primitives.

## Progress

- Added header-only `eq2::core` primitives for result/error values, configuration lookup, structured logging, monotonic clock access, deadline and interval timers, endian helpers, byte buffer writing, blocking queues, serial executor ownership, and cooperative shutdown signaling.
- Linked `eq2::core` to the source2 thread dependency so shared executor and queue primitives are available to higher-level modules without each module owning thread linkage separately.
- Added `eq2_core_tests` to CTest with coverage for queue close behavior, executor task ordering, manual-clock timers, config lookup, result errors, logging records, byte-buffer endian writes, and shutdown requests.
- Confirmed the Phase 2 characterization tests still pass alongside the new core tests.
- Consolidated source2 protocol byte-order usage onto the shared `eq2::core` endian helpers, keeping protocol-specific packet structure in `protocol` while avoiding duplicate infrastructure utilities.
- Expanded core test coverage for void results, endian reads and array overloads, and shutdown waiter wake-up behavior.
- Reconfirmed the Debug source2 build and all current CTest tests pass after the Phase 3 cleanup.

## Exit Criteria

- `eq2::core` has no source2 project-specific dependencies.
- Higher-level modules can use core utilities without circular dependencies.
- Core tests cover queue, time, config, and error primitives where applicable.
