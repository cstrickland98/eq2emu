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
- Added a `MariaDbConnection` adapter seam that stores MariaDB config and reports `unavailable` when the MariaDB C API adapter is not enabled in the current build.
- Kept login/world code insulated from raw database APIs by depending on repository interfaces.
- Extended `eq2_db_tests` to verify SQL repository query boundaries, parameter use, row mapping, and explicit unavailable behavior for the disabled MariaDB adapter.
- Verification:
  - `cmake --build build\source2 --config Debug --target eq2_db_tests`: passed.
  - `ctest --test-dir build\source2 -C Debug -R eq2_db_tests --output-on-failure`: passed.

## Dependency Note

The local workspace did not expose a MariaDB/MySQL C connector library through the source2 CMake dependency graph. This phase therefore completes the repository implementation and adapter seam, while keeping the concrete C API driver disabled instead of adding unverified include/library paths. Enabling the C API driver is a build-environment task once connector headers and libraries are available.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_db_tests eq2_login_characterization_tests
ctest --test-dir build\source2 -C Debug -R "db|login_characterization" --output-on-failure
```
