# Phase 6: DB Module

Status: complete.

## Purpose

Centralize database access behind controlled source2 interfaces.

## Scope

DB owns connection configuration, pooling, query execution, and repository implementations. Higher-level modules should not directly call raw MariaDB APIs.

## Deliverables

- Database config model.
- Connection pool.
- Query execution wrapper.
- Repository interfaces for account, login, world registration, character list, and zone bootstrap data.
- Test double or fake DB implementation.

## Progress

- Added `eq2::db::DatabaseConfig` for connection settings and pool sizing.
- Added `QueryRequest`, `QueryRow`, `QueryResult`, `QueryConnection`, `ConnectionPool`, and `AsyncQueryExecutor` wrappers.
- Added source2 repository contracts for login accounts, world registration, character lists, and zone bootstrap data.
- Added fake query and repository implementations for account authentication, world registration, character-list lookup, and zone bootstrap lookup.
- Updated login authentication to consume record-shaped source2 DB repositories without raw database APIs.
- Added `eq2_db_tests` covering config, connection-pool round robin behavior, async worker execution, and the critical fake repositories.
- Extended login characterization coverage with authentication through `eq2::db::FakeLoginAccountRepository`.
- Identified the critical Phase 6 hot paths as account lookup/creation, world account/version validation, character-list loading, and zone bootstrap loading; each now has an explicit repository boundary.

## Exit Criteria

- Login code can authenticate without raw DB API usage.
- Blocking DB work can run off owner executors and return results safely.
- Critical hot-path queries are identified and isolated.
