# Phase 5: Database Migrations And Updater

Status: complete.

## Purpose

Add an AzerothCore-style database lifecycle for EQ2Emu without redesigning the schema. The immediate goal is repeatable visibility and update ordering, not schema ownership perfection.

## Where Work Begins

Start with:

- `docs/eq2_vs_azerothcore_architecture.md` database lifecycle section.
- Existing database docs under `docs/database`.
- Existing SQL dump/import expectations from project docs.
- Source2 DB config and repository work from Phase 4.

## Work Entailed

- Define repository-owned database folder layout:
  - `database/login/base`
  - `database/login/updates`
  - `database/login/pending`
  - `database/world/base`
  - `database/world/updates`
  - `database/world/pending`
  - `database/custom`
- Define migration naming policy.
- Define applied-migration tracking table per DB.
- Add a standalone updater/list tool under `source2/tools` or `source2/apps`.
- Support:
  - list pending,
  - dry run,
  - apply,
  - record baseline,
  - fail clearly on partial update,
  - separate login/world DB config.
- Add tests for migration ordering and failure handling using fake DB/query executor.
- Document how existing external dumps become the baseline.

## Deliverables

- `database/` migration directory scaffold.
- Migration naming and tracking documentation.
- Source2 DB updater tool target.
- Unit tests for migration discovery/order/tracking.
- Dry-run/list/apply behavior.
- Baseline strategy for existing login/world databases.

## Exit Criteria

- Updater can list and dry-run migrations without a live DB.
- Updater can apply migrations against a configured test DB or fake executor.
- Migration order is deterministic and tested.
- Existing database dump workflow has a documented baseline path.
- Server startup does not auto-apply migrations unless explicitly decided in a later phase.

## Progress

- Created the repository-owned database migration layout under `database/`:
  - `login/base`, `login/updates`, `login/pending`
  - `world/base`, `world/updates`, `world/pending`
  - `custom`
- Added layout, naming, tracking, and baseline documentation in `database/README.md` and per-directory README files.
- Added `eq2/db/migrations.h` with:
  - login/world database kind names,
  - timestamped migration id validation,
  - deterministic `.sql` discovery and ordering,
  - pending migration calculation,
  - SQL file loading,
  - migration apply loop with explicit failed-migration reporting,
  - memory migration tracking store,
  - SQL-backed schema-version store boundary,
  - `source2_schema_version` table creation helper,
  - baseline recording helper.
- Added `source2/tools/db_updater` and wired it into the source2 build.
- Updater supports:
  - `--database login|world`,
  - `--path`,
  - `--list`,
  - `--dry-run`,
  - `--apply` through the in-memory executor path,
  - `--record-baseline`,
  - `--applied` pre-marking for repeatable list/dry-run checks.
- Added DB tests for:
  - migration discovery filtering,
  - deterministic lexical ordering,
  - naming policy validation,
  - pending migration filtering,
  - successful fake-query apply and tracking,
  - partial apply failure behavior,
  - SQL schema-version tracking query boundaries.

Dependency note:

- Live MariaDB apply remains blocked on the Phase 4 adapter dependency. The updater and migration library are structured so a real `QueryConnection` can be plugged in once the MariaDB C/C++ connector is present, but this phase does not claim live DB apply against MariaDB.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_db_tests
ctest --test-dir build\source2 -C Debug -R "db|migration|updater" --output-on-failure
```

Additional verification run:

```powershell
cmake -S . -B build\source2
cmake --build build\source2 --config Debug --target eq2_db_tests eq2_db_updater
ctest --test-dir build\source2 -C Debug -R eq2_db_tests --output-on-failure
build\source2\source2\tools\db_updater\Debug\eq2_db_updater.exe --database login --path database\login\updates --list
build\source2\source2\tools\db_updater\Debug\eq2_db_updater.exe --database world --path database\world\updates --dry-run
build\source2\source2\tools\db_updater\Debug\eq2_db_updater.exe --database login --record-baseline external_dump_20260515
```
