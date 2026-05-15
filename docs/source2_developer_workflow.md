# Source2 Developer Workflow

## Local CI

Run the same source2 path expected by CI:

```powershell
.\scripts\source2_ci.ps1
```

That configures, builds, and runs CTest with apps and tools enabled.

Optional loopback smoke tests are gated behind:

```powershell
.\scripts\source2_ci.ps1 -LiveSmoke
```

## Manual Commands

```powershell
cmake -S . -B build\source2 -DEQ2EMU_BUILD_SOURCE2=ON -DEQ2_SOURCE2_BUILD_APPS=ON -DEQ2_SOURCE2_BUILD_TOOLS=ON
cmake --build build\source2 --config Debug
ctest --test-dir build\source2 -C Debug --output-on-failure
```

## Useful Targets

- `eq2_login_server`
- `eq2_world_server`
- `eq2_db_updater`
- `eq2_core_tests`
- `eq2_protocol_tests`
- `eq2_net_tests`
- `eq2_db_tests`
- `eq2_scripting_tests`
- `eq2_zone_runtime_tests`
- `eq2_login_server_tests`
- `eq2_world_session_tests`

## Build Options

- `EQ2EMU_BUILD_SOURCE2`: enables source2 at the root project.
- `EQ2_SOURCE2_BUILD_APPS`: builds source2 app executables.
- `EQ2_SOURCE2_BUILD_TOOLS`: builds source2 tool executables.
- `EQ2_SOURCE2_ENABLE_LIVE_SMOKE_TESTS`: registers loopback smoke tests with CTest.
- `BUILD_TESTING`: enables unit and characterization tests.

## Live Smoke Commands

```powershell
build\source2\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
build\source2\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```

These are loopback-only and use isolated test repository connections. They do not require a live MariaDB server.
