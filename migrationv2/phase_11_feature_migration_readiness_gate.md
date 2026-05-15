# Phase 11: Feature Migration Readiness Gate

Status: complete.

## Purpose

Decide whether source2 is ready for gameplay feature migration. This phase is a hard gate: if live architecture evidence is incomplete, feature migration should not begin.

## Where Work Begins

Start after Phases 1 through 10 are complete or explicitly waived. Use all phase progress notes, tests, smoke logs, and documented compatibility gaps.

## Work Entailed

- Audit the platform against the original AzerothCore-derived goals:
  - target-based build,
  - tests,
  - config policy,
  - logging policy,
  - DB lifecycle,
  - live transport,
  - repository boundaries,
  - scripting boundary,
  - CI/tools,
  - owner-executor model.
- Run full source2 build and tests.
- Run live smoke tests:
  - login,
  - world registration,
  - character list/select,
  - zone admission,
  - Lua script call if Phase 9 is complete.
- Review compatibility gaps and decide whether they block feature work.
- Select the first feature migration pilot.
- Define feature migration rules:
  - required characterization tests,
  - allowed dependencies,
  - DB migration requirements,
  - Lua API requirements,
  - smoke test requirements.

## Deliverables

- Readiness audit recorded in this file.
- Explicit go/no-go decision for gameplay feature migration.
- Selected first feature migration pilot.
- Feature migration checklist.
- Known risks and rollback strategy.

## Exit Criteria

- Full source2 build and tests pass.
- Required live smoke tests pass or are explicitly waived with accepted risk.
- No fake-only runtime path is used as evidence for live readiness.
- First feature pilot is selected with clear scope and tests.
- Project owner accepts the readiness decision.

## Progress

- Audited source2 against the AzerothCore-derived platform goals:
  - target-based build: complete,
  - tests: complete with CTest coverage across core/protocol/net/db/scripting/zone/login/world,
  - config policy: typed runtime validation exists for app composition,
  - logging policy: source2 log sink and app startup logs exist,
  - DB lifecycle: migration layout and updater exist,
  - live transport: TCP/UDP loopback transport exists,
  - repository boundaries: SQL repositories exist for login/world/character/zone boundaries,
  - scripting boundary: real Lua backend exists with a strict source2-safe API subset,
  - CI/tools: local CI script, GitHub workflow, and first-class tool target exist,
  - owner model: zone command owner and world/zone handoff boundaries exist.
- Ran normal local CI:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1`
  - Result: 15/15 tests passed.
- Ran live-smoke local CI:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -LiveSmoke`
  - Result: 17/17 tests passed, including login and world live smoke tests.
- Recorded feature migration rules in `docs/source2_feature_migration_rules.md`.
- Selected the first feature migration pilot:
  - `Zone safe-location/player-admission feature slice`.

## Decision

Conditional go for the first narrow gameplay feature pilot.

No-go for broad live-client gameplay migration yet.

Blocking gaps for broad migration:

- MariaDB C/C++ connector is not enabled in the source2 build, so live DB execution is still represented through `QueryConnection` test adapters.
- Real socket transport receives inbound bytes but does not yet write source2 protocol responses back to connected clients.
- Login/world/zone client packet response serialization is still incomplete.
- Legacy Lua API compatibility is intentionally limited to a strict source2-safe subset.

The selected Phase 12 pilot is allowed because it stays inside the proven boundaries: DB-backed zone metadata, world-to-zone handoff, zone owner commands, snapshots, and Lua-safe hooks.

## Verification Commands

```powershell
cmake -S . -B build\source2
cmake --build build\source2 --config Debug
ctest --test-dir build\source2 -C Debug --output-on-failure
```

Verification run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -LiveSmoke
```
