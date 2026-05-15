# Phase 4: MariaDB Repositories

Status: complete.

## Purpose

Replace source2's fake-only database boundary with real MariaDB-backed repositories for the login and world session flows.

## Where Work Begins

Start with:

- `source2/db/include/eq2/db/config.h`
- `source2/db/include/eq2/db/query.h`
- `source2/db/include/eq2/db/repositories.h`
- `source2/db/include/eq2/db/fake_database.h`
- Legacy references:
  - `source/LoginServer/LoginDatabase.*`
  - `source/WorldServer/WorldDatabase.*`
  - `source/common/database.*`
  - `source/common/DatabaseNew.*`
- Table notes in `migration/phase_1_database_inventory.md`.

## Work Entailed

- Add MariaDB dependency discovery to CMake.
- Implement real connection creation and teardown.
- Implement prepared or parameterized query execution where supported.
- Implement connection pool behavior with clear ownership and shutdown.
- Implement repositories for:
  - login account lookup,
  - account creation if enabled,
  - world account/version validation,
  - banned IP/address checks required by world registration,
  - character list rows,
  - character appearance and equipment rows or adapters,
  - zone bootstrap metadata.
- Preserve fake repositories for tests.
- Add integration tests that can run against an isolated test database when configured.
- Ensure blocking DB work runs off owner executors or is clearly isolated.
- Document required schema/table assumptions.

## Deliverables

- MariaDB connection adapter.
- Real query executor.
- Real repository implementations for login, world registration, character list, and zone bootstrap.
- Test DB configuration path.
- Integration tests gated by an explicit option or environment variable.
- Documentation of required DB tables and minimum seed data for tests.

## Exit Criteria

- Login authentication can run through a MariaDB repository without raw DB calls in login code.
- World registration can run through a MariaDB repository.
- Character list retrieval can run through a MariaDB repository.
- Zone bootstrap data can be loaded through a MariaDB repository.
- DB tests cover fake repositories by default and real repositories when test DB config is present.

## Progress

- Added `source2/db/include/eq2/db/sql_repositories.h`.
- Added SQL-backed repository implementations over `eq2::db::QueryConnection`:
  - `SqlLoginAccountRepository`,
  - `SqlWorldRegistrationRepository`,
  - `SqlCharacterListRepository`,
  - `SqlZoneBootstrapRepository`.
- Added a `MariaDbConnection` adapter seam with a real MariaDB C API path when
  connector headers/libraries are available, and explicit `unavailable`
  behavior when the adapter is not enabled in a non-connector build.
- Hardened prepared statement result fetching by binding scratch buffers before
  `mysql_stmt_fetch` and then reading complete column values through
  `mysql_stmt_fetch_column`.
- Extended source2 CMake dependency discovery for MariaDB through vcpkg manifest mode or an explicit `EQ2_MARIADB_ROOT` SDK path.
- Kept login/world code insulated from raw database APIs by depending on repository interfaces.
- Extended `eq2_db_tests` to verify SQL repository query boundaries, parameter use, row mapping, and explicit unavailable behavior for the disabled MariaDB adapter.
- Verification:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify`: passed with 110 registered tests, 109 executed/passed, and 1 skipped opt-in target live login gate.

## Dependency Note

The current local vcpkg-backed build enables MariaDB Connector/C through
`libmariadb`, builds `MariaDbConnection`, and deploys the runtime DLLs beside
the owner-facing source2 executables. Live validation against the requested
MariaDB host still requires TCP `3306` reachability to `192.168.1.243`.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```
