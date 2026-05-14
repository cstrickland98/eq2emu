# Phase 6: DB Module

Status: planned.

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

## Exit Criteria

- Login code can authenticate without raw DB API usage.
- Blocking DB work can run off owner executors and return results safely.
- Critical hot-path queries are identified and isolated.

