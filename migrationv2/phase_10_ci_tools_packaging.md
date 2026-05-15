# Phase 10: CI, Tools, And Packaging Structure

Status: complete.

## Purpose

Adopt the AzerothCore-inspired platform workflow pieces that make migration sustainable: repeatable tools, CI, packaging conventions, and module/content layout.

## Where Work Begins

Start after Phases 1 through 5 establish real app, net, DB, and updater foundations. Use:

- Root CMake project.
- Source2 test targets.
- DB updater tool from Phase 5.
- `docs/eq2_vs_azerothcore_architecture.md` recommendations on CI, tools, and module packaging.

## Work Entailed

- Add `source2/tools` CMake support if not already present.
- Move standalone utilities into tool targets.
- Add build options for:
  - source2 apps,
  - source2 tools,
  - tests,
  - optional live integration tests.
- Add CI workflow or local script equivalent for:
  - configure,
  - build,
  - CTest,
  - optional format/lint checks if available.
- Define module/content package convention:
  - source,
  - Lua,
  - SQL,
  - config,
  - docs.
- Add generated/version metadata if useful for logs and diagnostics.
- Document developer verification workflow.

## Deliverables

- `source2/tools` target support.
- DB updater or other tools integrated into build.
- CI workflow or documented local CI script.
- Build options for apps/tools/tests/integration tests.
- Module/content packaging convention documentation.
- Developer verification documentation.

## Exit Criteria

- A clean checkout can configure, build, and run source2 tests through one documented workflow.
- Tools are first-class CMake targets.
- Optional live integration tests are gated so normal CTest remains reliable.
- Module/content package layout is documented and reviewed.

## Progress

- Added source2 build options:
  - `EQ2_SOURCE2_BUILD_APPS`
  - `EQ2_SOURCE2_BUILD_TOOLS`
  - `EQ2_SOURCE2_ENABLE_LIVE_SMOKE_TESTS`
- Kept normal CTest reliable by leaving live smoke tests off unless explicitly enabled.
- Registered optional CTest smoke tests for:
  - `eq2_login_server --smoke-login-live`
  - `eq2_world_server --smoke-world-live`
- Added `scripts/source2_ci.ps1`.
  - Configures source2.
  - Builds apps, tools, and tests.
  - Runs CTest.
  - Enables loopback smoke tests only with `-LiveSmoke`.
- Added `.github/workflows/source2.yml` for Windows configure/build/test.
- Added `eq2/core/version.h` and compile-time version metadata for app diagnostics.
- Added developer workflow documentation in `docs/source2_developer_workflow.md`.
- Added module/content package convention documentation in `docs/source2_module_package_layout.md`.
- Confirmed `source2/tools` is wired and `eq2_db_updater` is a first-class CMake target.

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
