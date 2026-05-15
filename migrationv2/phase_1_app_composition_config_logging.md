# Phase 1: App Composition, Config, And Logging

Status: complete.

## Purpose

Make source2 apps look like real server composition roots instead of test harnesses. Apps should load typed runtime config, construct real services through interfaces, configure logging, start/stop cleanly, and keep fake/test wiring out of production code paths.

## Where Work Begins

Start with:

- `source2/apps/login_server/main.cpp`
- `source2/apps/world_server/main.cpp`
- `source2/core/include/eq2/core/config.h`
- `source2/core/include/eq2/core/log.h`
- Existing runtime examples under `server/`
- Legacy config inputs identified in `migration/phase_1_runtime_inventory.md`

The current app smoke modes use fake repositories and fixture data. Preserve smoke coverage, but move test-only wiring out of the default app path.

## Work Entailed

- Define typed config models for:
  - shared process/runtime settings,
  - login server bind/listen settings,
  - world server bind/listen/login-connection settings,
  - login DB config,
  - world DB config,
  - logging settings.
- Add config loading adapters for current file formats where practical:
  - `server_config.json`,
  - `login_db.ini`,
  - `world_db.ini`,
  - `log_config.xml` if logging integration requires it.
- Add config validation with severity:
  - missing optional key,
  - missing required key,
  - malformed value,
  - incompatible settings.
- Add a startup validation summary.
- Make apps production-shaped:
  - parse CLI flags,
  - load config,
  - build logger,
  - build DB service placeholders or real services when Phase 4 exists,
  - build net service placeholders or real services when Phase 2 exists,
  - wire login/world service,
  - handle shutdown.
- Move fake smoke wiring into tests or an explicitly test-only compile path.
- Add app-level tests for config validation and failure behavior.

## Deliverables

- `eq2::core` or app-owned typed config loader interfaces.
- Login/world app config structs and validation results.
- Logging initialization policy.
- Updated app main files that avoid fake production wiring.
- Tests for config defaults, required values, invalid values, and app wiring behavior.
- Updated documentation describing source2 app startup.

## Exit Criteria

- Default source2 app startup path no longer depends on fake DB repositories or fixture data.
- Config validation catches missing and malformed required settings.
- Login and world app main files are thin composition roots.
- App tests cover config and startup failure paths.
- Smoke tests still exist, but they are clearly test-mode or test-target behavior.

## Progress

- Added `eq2::core::RuntimeConfig`, typed endpoints, logging settings, config validation issues, and runtime config loading in `source2/core/include/eq2/core/runtime_config.h`.
- Added `eq2::core::ConsoleLogSink` so app composition roots can initialize a concrete log sink without reaching into legacy logging.
- Updated `eq2_login_server` and `eq2_world_server` default app paths to:
  - construct runtime config from a `ConfigProvider`,
  - validate config before startup reporting,
  - initialize console logging,
  - report the configured source2 endpoints.
- Removed fake DB repositories and fixture smoke logic from the default app startup paths.
- Added app failure behavior using `--strict-config`, which intentionally injects an invalid port and exits with validation failure.
- Extended core tests for console log formatting and runtime config loading/rejection.
- Verification:
  - `cmake --build build\source2 --config Debug --target eq2_core_tests eq2_login_server eq2_world_server`: passed.
  - `ctest --test-dir build\source2 -C Debug -R "eq2_core_tests|eq2_login_server_tests|eq2_world_session_tests" --output-on-failure`: passed.
  - `eq2_login_server.exe`: reports validated source2 login wiring.
  - `eq2_world_server.exe`: reports validated source2 world/login wiring.
  - `eq2_login_server.exe --strict-config`: rejects invalid config as expected.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_login_server eq2_world_server
ctest --test-dir build\source2 -C Debug -R "config|app|login_server|world_session" --output-on-failure
```
