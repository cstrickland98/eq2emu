# Phase 3: Core Module

Status: in progress.

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

## Exit Criteria

- `eq2::core` has no source2 project-specific dependencies.
- Higher-level modules can use core utilities without circular dependencies.
- Core tests cover queue, time, config, and error primitives where applicable.
