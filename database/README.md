# EQ2Emu Database Migration Layout

This directory is the source2 database lifecycle scaffold. Existing external login/world SQL dumps remain the baseline until they are imported and recorded through the updater.

## Layout

- `login/base`: baseline login DB notes or base SQL.
- `login/updates`: accepted login DB migrations.
- `login/pending`: new login DB migrations waiting for review.
- `world/base`: baseline world DB notes or base SQL.
- `world/updates`: accepted world DB migrations.
- `world/pending`: new world DB migrations waiting for review.
- `custom`: operator-local or shard-local migration notes.

## Naming

Use lexical ordering:

```text
YYYYMMDDHHMM_description.sql
```

Example:

```text
202605150900_add_source2_schema_version.sql
```

## Tracking

The source2 updater records applied migrations in a schema-version table for each database. The updater starts as a standalone tool; server startup should not auto-apply migrations until that policy is explicitly accepted.
