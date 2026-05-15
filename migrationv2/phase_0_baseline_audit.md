# Phase 0: Baseline Audit And V2 Scope

Status: complete.

## Purpose

Establish an honest baseline for source2 after migration v1. This phase prevents v2 from inheriting overstated completion claims and defines the evidence required before feature migration can begin.

## Where Work Begins

Start from the current `source2` tree and the existing migration documents:

- `migration/phase_4.md` through `migration/phase_12.md`.
- `docs/eq2_vs_azerothcore_architecture.md`.
- `docs/source2_migration_plan.md`.
- `docs/source2_legacy_retirement.md`.
- Current source2 CMake targets and tests.

Do not modify runtime architecture in this phase unless an audit issue prevents later phases from being scoped.

## Work Entailed

- Review every v1 phase status against actual code, tests, app behavior, and docs.
- Categorize each v1 result as:
  - complete and usable as-is,
  - boundary/scaffold complete,
  - incomplete,
  - blocked by missing live integration.
- Record where fake DB, in-memory net, test-only smoke, or documentation-only evidence is being used.
- Define the minimum live-readiness gate before gameplay migration.
- Confirm current build and test commands.
- Confirm the working-tree baseline and note any uncommitted source2/migration changes.

## Deliverables

- A short audit section added to this file under `Progress`.
- A checklist of v2 blockers before feature migration.
- Confirmation of current build/test command output.
- Updated phase status if this audit discovers that a v2 phase must be split.

## Exit Criteria

- Every v1 phase from 4 through 12 is classified with real evidence.
- Fake/in-memory/test-only evidence is explicitly separated from production-ready evidence.
- The v2 phase list is accepted as the next architecture plan.
- The current source2 build and tests are run and recorded.

## Progress

- Classified v1 phases 4-12 against current artifacts:
  - Phase 4 protocol: complete enough to reuse as a source2 protocol foundation; `eq2_protocol_tests` passes and protocol remains dependency-clean.
  - Phase 5 net: boundary complete, but real transport was missing at the start of v2; net used in-memory session abstractions.
  - Phase 6 DB: boundary complete, but real MariaDB-backed repositories were missing at the start of v2; DB used interfaces, worker wrappers, and fakes.
  - Phase 7 login: vertical-slice boundary complete, but live-client parity was missing; smoke used a fixture and fake repositories.
  - Phase 8 world: session boundary complete, but login-to-world live flow was missing; smoke used fake character data and fake handoff.
  - Phase 9 zone: owner-command boundary complete, but live zone bootstrap and real client update details were missing.
  - Phase 10 scripting: boundary complete, but real Lua backend was missing.
  - Phase 11 gameplay: owner registry complete, but most gameplay parity remained legacy.
  - Phase 12 retirement: documentation complete, but no subsystem was retired.
- Identified v2 blockers before feature migration:
  - production-shaped app composition and config validation,
  - real TCP/UDP transport,
  - EQStream protocol pipeline over transport,
  - real or adapter-backed DB repositories,
  - database migration/updater workflow,
  - live login/world/zone smoke coverage,
  - Lua backend integration,
  - tools/CI/package structure,
  - readiness gate before first feature migration.
- Confirmed current source2 verification:
  - `cmake -S . -B build\source2`: passed.
  - `cmake --build build\source2 --config Debug`: passed.
  - `ctest --test-dir build\source2 -C Debug --output-on-failure`: 14/14 passed.

## Verification Commands

```powershell
cmake -S . -B build\source2
cmake --build build\source2 --config Debug
ctest --test-dir build\source2 -C Debug --output-on-failure
```
