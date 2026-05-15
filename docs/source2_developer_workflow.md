# Source2 Developer Workflow

## Local CI

Run the same source2 path expected by CI:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1
```

That configures, builds, and runs CTest with apps and tools enabled.

Optional loopback smoke tests are gated behind:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -LiveSmoke
```

To verify the MariaDB Connector/C path through vcpkg, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

## Manual Commands

```powershell
cmake -S . -B build\source2 -DEQ2EMU_BUILD_SOURCE2=ON -DEQ2_SOURCE2_BUILD_APPS=ON -DEQ2_SOURCE2_BUILD_TOOLS=ON
cmake --build build\source2 --config Debug
ctest --test-dir build\source2 -C Debug --output-on-failure
```

## vcpkg MariaDB Connector

The source2 vcpkg manifest lives at `source2/vcpkg.json` and declares `libmariadb`
for MariaDB Connector/C. Configure with the vcpkg CMake toolchain to install and
link it:

```powershell
cmake -S . -B build\source2 -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" -DEQ2EMU_BUILD_SOURCE2=ON
```

`EQ2_SOURCE2_ENABLE_MARIADB` defaults to `ON`. If the vcpkg package is available,
source2 links `unofficial::libmariadb` and enables the live `MariaDbConnection`
adapter. Without the package, source2 keeps the disabled adapter path so normal
CI and non-database tests still build.

You can also point CMake at a manually installed Connector/C SDK:

```powershell
cmake -S . -B build\source2 -DEQ2EMU_BUILD_SOURCE2=ON -DEQ2_MARIADB_ROOT="C:\path\to\mariadb-connector-c"
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
- `EQ2_SOURCE2_ENABLE_MARIADB`: enables MariaDB Connector/C when `libmariadb` is available.
- `EQ2_MARIADB_ROOT`: optional Connector/C SDK root used when not building through vcpkg.
- `EQ2_SOURCE2_ENABLE_LIVE_SMOKE_TESTS`: registers loopback smoke tests with CTest.
- `BUILD_TESTING`: enables unit and characterization tests.

## Live Smoke Commands

```powershell
build\source2\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
build\source2\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```

These are loopback-only and use isolated test repository connections. They do not require a live MariaDB server.

## Source2 Login Server

For the concise owner-facing build/configure/run/client steps, see the
[Source2 Login Server Guide](source2_login_server_guide.md).

Use the vcpkg-enabled build when running against MariaDB. The login server can
load INI config, environment variables, or CLI overrides. Start from:

```powershell
source2\config\login_server.ini.example
```

For the current target DB host from this migration, start from the no-secret
template:

```powershell
source2\config\login_server.eq2emu-target.ini.example
```

For real LoginStream clients, keep `[login] opcode_source = database` and
`opcode_width = 1`, and set `[login] client_version` to the client build you
intend to support. On startup, source2 reads `OP_LoginRequestMsg`,
`OP_LoginReplyMsg`, `OP_WorldListMsg`, `OP_AllWSDescRequestMsg`, and
`OP_AllCharactersDescRequestMsg`, and `OP_AllCharactersDescReplyMsg` from the
login database `opcodes` table for the configured client version. Manual/debug
overrides are still available by setting `opcode_source = config`:

```powershell
--login-opcode-source config --login-opcode-width 1 --login-request-opcode 0x01 --login-reply-opcode 0x02 --login-world-list-opcode 0x03 --login-all-worlds-request-opcode 0x04 --login-characters-request-opcode 0x05 --login-characters-reply-opcode 0x06
```

Validate owner config without opening sockets:

```powershell
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --validate-config --config source2\config\login_server.ini.example
```

Check the login database connection and account credentials without opening a
server socket:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_USERNAME = "<account>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --check-login-db --db-host 192.168.1.243 --db-name eq2ls --db-user eq2emu
```

The repeatable live verification wrapper runs the TCP reachability check, login
DB/account/opcode check, world DB schema check, and optionally the DB-backed
login smoke and real `--serve`/`--probe-login` startup path:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1
```

This command also verifies the required login opcode rows are present in the
`opcodes` table for the requested `--client-version`, verifies the matching
`version_range1`/`version_range2` opcode range that the live server will accept,
and validates the legacy login character-list tables/columns used by the client
select screen.

For an owner-facing run from a normal PowerShell window, use the transcript
wrapper. It performs the same live verifier path and writes a timestamped log
under `artifacts\source2-live-login-preflight`:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_owner_live_login_preflight.ps1
```

For an owner-facing CI-style gate from the same normal PowerShell window, use
the CTest wrapper. It runs the no-secret TCP check first, then exports the
non-secret target parameters as `EQ2_SOURCE2_TARGET_*`, enables the opt-in
CTest live target, and runs `source2_target_live_login_ctest`:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify
```

For the stronger non-empty world-list gate:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_owner_live_login_preflight.ps1 -RunWorldServe
```

The CTest wrapper also supports the world-list gate:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe
```

If the check fails before authentication with a TCP error, resolve the database
host before debugging source2 login code:

1. From the source2 host, verify the port is reachable:

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_check_mariadb_tcp.ps1
   Test-NetConnection -ComputerName 192.168.1.243 -Port 3306
   ```

   The source2 helper does not need DB credentials. It prints the TCP result,
   lower-level socket classification, and local IPv4 candidates to use for
   MariaDB firewall/grant rules.

2. On the MariaDB host, confirm MariaDB is listening on TCP 3306 and is bound to
   the LAN address or `0.0.0.0`, not only `127.0.0.1`.
3. Allow inbound TCP 3306 from the source2 host through the MariaDB host
   firewall.
4. Confirm the DB user is allowed from the source2 host, for example
   `eq2emu`@`%` or `eq2emu`@`<source2-host-ip>`, and has access to `eq2ls`.
5. Restart MariaDB after changing bind or grant settings, then rerun
   `--check-login-db`.

For a command-by-command remediation guide, see
`docs/source2_live_login_blocker_resolution.md`.

For the current evidence checklist and completion gates, see
`docs/source2_live_login_readiness_audit.md`.

Create deploy-time config files outside the source templates before adding
secrets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_prepare_deploy_config.ps1 -OutputDir artifacts\source2-deploy-config
```

Run a DB-backed login smoke that starts an ephemeral source2 login server, logs
in once, and verifies the post-login world-list, character-list, and follow-up
login reply flow:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_USERNAME = "<account>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-mariadb --db-host 192.168.1.243 --db-name eq2ls --db-user eq2emu
```

Start the real source2 login server:

```powershell
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --serve --config source2\config\login_server.ini.example
```

For the current target environment, the checked owner startup wrapper validates
config, verifies the target login DB/account/opcodes when login credentials are
provided, then starts `--serve`:

```powershell
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-login-config.ini>
```

Use `-SkipAccountPreflight` when starting a server without a test account
password. The wrapper still validates config, checks MariaDB TCP reachability,
supplies the DB password, runs `eq2_login_server --check-login-db
--skip-account-auth` to verify DB/schema/opcodes/version ranges, and then lets
`eq2_login_server --serve` repeat its startup checks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-login-config.ini> -SkipAccountPreflight
```

Set `db.password` in `<deploy-login-config.ini>` outside source control, or set
`EQ2_DB_PASSWORD` before running the wrapper. The wrapper prefers explicit
arguments/environment values, but accepts a non-placeholder config password for
the long-running login server path.

Use `-DryRun` first to validate the config, list any environment secret names
that are still required, and print the start command without opening DB or
server sockets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-login-config.ini> -DryRun
```

When serving, source2 binds both UDP and TCP on the configured login port: UDP
for login clients and TCP for world/interserver registration. A world server
that sends legacy `ServerOP_LSInfo` over TCP is registered in memory and is
included in subsequent login world-list responses.

On Windows hosts, allow both protocols on the configured login port before
expecting external clients or world servers to connect. For the target template
port `9100`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_configure_login_firewall.ps1 -DryRun
```

If the dry run matches the intended policy, rerun it from an elevated
PowerShell prompt without `-DryRun`, or create the equivalent rules manually:

```powershell
New-NetFirewallRule -DisplayName "Source2 Login UDP 9100" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 9100
New-NetFirewallRule -DisplayName "Source2 Login TCP 9100" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 9100
```

The source2 verifier uses loopback for its serve/probe check, so these host
firewall rules are needed for real clients or a world server on another
machine, not for the local verifier itself.

For the current classic client range, source2 emits the legacy keyed EQStream
CRC on non-session replies and serializes client version 546 login replies and
world-list payloads with the corresponding `server/LoginStructs.xml` layouts.
After a successful login, source2 responds to the legacy
`OP_AllWSDescRequestMsg` post-login request with a world list, an empty
character-list response, and the legacy follow-up login reply code `10`.

Probe a running server with the source2 UDP login frame:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_USERNAME = "<account>"
$env:EQ2_LOGIN_PASSWORD = "<account-password>"
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --probe-login --connect-host 127.0.0.1 --connect-port 9100 --db-host 192.168.1.243 --db-name eq2ls --db-user eq2emu
```

For the current target environment, use the checked probe wrapper so DB-backed
opcode loading and login credentials are supplied consistently:

```powershell
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-login-config.ini> -ConnectHost <login-host> -ConnectPort 9100 -ExpectedWorldCount 0
```

Set `db.password` in `<deploy-login-config.ini>` outside source control, or set
`EQ2_DB_PASSWORD` before running the wrapper. The login account password remains
an argument/environment secret and is not stored in the deploy login config.

Use `-DryRun` first to validate the probe config, list any environment secret
names that are still required, and print the masked probe command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-login-config.ini> -ConnectHost <login-host> -ConnectPort 9100 -ExpectedWorldCount 0 -DryRun
```

The probe performs a source2 session handshake, sends credentials, sends the
post-login all-worlds request, and expects world-list, character-list, and
post-world-list login replies before it exits successfully.

When probing a server configured with `opcode_source = database`, pass the same
config file so the probe reads the same opcode rows before sending the UDP
login frame. For isolated source2 harness probes without DB access, set
`--login-opcode-source config` and provide the manual opcode values.

## Source2 World Database

The world server can validate the configured world database separately from the
login database. Start from:

```powershell
source2\config\world_server.ini.example
```

For the current target DB host from this migration, start from the no-secret
template:

```powershell
source2\config\world_server.eq2emu-target.ini.example
```

Check the world DB connection and the zone-bootstrap schema:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_world_server.exe --check-world-db --db-host 192.168.1.243 --db-name eq2emu --db-user eq2emu
```

To advertise a source2 world in the login server's world list, configure
`[world] account` and `password` to match a `login_worldservers` row in the
login database, set `[world] advertised_address` to a client-reachable address
instead of the bind wildcard `0.0.0.0`, configure `[login] remote_address` /
`remote_port` to the running login server, then start:

```powershell
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_world_server.exe --serve --config source2\config\world_server.ini.example
```

For the current target environment, the checked owner startup wrapper validates
config, verifies world DB reachability/schema, checks source2 login TCP
reachability, then starts `--serve` without printing secrets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_world_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-world-config.ini>
```

Set `world_db.password`, `world.account`, and `world.password` in
`<deploy-world-config.ini>` outside source control, or provide the corresponding
`EQ2_DB_PASSWORD`, `EQ2_WORLD_ACCOUNT`, and `EQ2_WORLD_PASSWORD` environment
variables. The wrapper accepts non-placeholder config values and does not print
them.

Use `-DryRun` first to validate the world config, list any environment secret
names that are still required, and print the start command without opening DB,
login, or world sockets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_world_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-world-config.ini> -DryRun
```

The current source2 world app starts the world TCP listener and sends legacy
`ServerOP_LSInfo` to login. Character play remains a staged feature-migration
area; this serve path is intended to support login world-list registration.
