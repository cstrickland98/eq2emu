# Phase 3: Core Module

Status: planned.

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

## Exit Criteria

- `eq2::core` has no source2 project-specific dependencies.
- Higher-level modules can use core utilities without circular dependencies.
- Core tests cover queue, time, config, and error primitives where applicable.

