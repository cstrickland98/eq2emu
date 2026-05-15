# Source2 Feature Migration Rules

## Readiness Decision

Source2 is ready for the first narrow gameplay feature pilot.

Source2 is not yet ready for broad live-client feature migration. Broad feature work remains gated by live MariaDB connector enablement, client socket write support, and packet response serialization.

## Required Checklist For Each Feature

- Name the legacy source files and docs being characterized.
- Add or update characterization tests before changing behavior.
- Keep ownership explicit:
  - zone-owned state mutates through zone commands,
  - player-owned state mutates through session/player services,
  - world-owned shared state mutates through world services.
- Use source2 repositories for persistent data access.
- Add SQL migrations under `database/<login|world>/pending` for schema changes.
- Use `ScriptBackend` and source2-safe Lua APIs for scripts.
- Do not call legacy globals from source2 feature code.
- Add unit tests for pure logic and integration tests for cross-module boundaries.
- Add or update smoke coverage if the feature touches login, world, zone admission, or Lua.
- Document unsupported client packet or Lua API gaps in the feature phase file.

## Allowed Pilot Scope

The first pilot must be narrow, observable, and reversible. It should avoid packet serialization that is not already supported by source2.

Selected pilot:

```text
Zone safe-location/player-admission feature slice
```

This uses DB-backed zone metadata, source2 world-to-zone handoff, zone owner commands, snapshots, and Lua-safe script hooks without requiring broad client packet parity.

## Rollback Strategy

- Keep legacy production paths intact.
- Keep pilot code isolated to source2 modules.
- Disable new smoke tests or feature entry points without deleting code if a regression is found.
- Revert SQL from `pending` before promotion to `updates` if the feature changes schema.
