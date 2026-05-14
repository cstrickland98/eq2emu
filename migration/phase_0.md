# Phase 0: Scaffold

Status: complete.

## Purpose

Create the source2 migration foundation without changing legacy server behavior.

## Completed Work

- Added the high-level migration plan in `docs/source2_migration_plan.md`.
- Created the `source2` architecture tree.
- Added root-level CMake support.
- Added target-based source2 module definitions.
- Added placeholder `eq2_login_server` and `eq2_world_server` executables.
- Kept the legacy `source` tree unchanged.

## Deliverables

- `CMakeLists.txt`
- `cmake/Eq2CompilerOptions.cmake`
- `cmake/Eq2Dependencies.cmake`
- `source2/CMakeLists.txt`
- Source2 module directories:
  - `core`
  - `protocol`
  - `net`
  - `db`
  - `login`
  - `world`
  - `zone`
  - `scripting`
  - `apps`
  - `tools`

## Verification

The scaffold should configure and build with:

```powershell
cmake -S . -B build\source2
cmake --build build\source2
```

The placeholder executables should run:

```powershell
build\source2\source2\apps\Debug\eq2_login_server.exe
build\source2\source2\apps\Debug\eq2_world_server.exe
```

## Exit Criteria

- Source2 tree exists.
- CMake configuration succeeds.
- Placeholder app targets build.
- Legacy source is not modified.

