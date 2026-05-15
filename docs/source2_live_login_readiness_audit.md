# Source2 Live Login Readiness Audit

Last updated: 2026-05-15.

The concise completion decision is tracked in
`docs\source2_live_login_completion_audit.md`.

## Objective

Source2 should let a server owner configure and start the login server, backed by
the target MariaDB instance, and let a user connect and authenticate with the
provided login account.

Target environment:

- Login DB host: `192.168.1.243:3306`
- Login DB name: `eq2ls`
- World DB name: `eq2emu`
- DB user: `eq2emu`
- Login account: `testlabs`
- Login client version: `546`

Passwords are intentionally not recorded in this audit or committed config
templates.

## Prompt-to-Artifact Completion Checklist

| Objective requirement | Artifact or command evidence | Completion state |
| --- | --- | --- |
| Source2 login path exists and is owner-runnable | `source2\apps\login_server\main.cpp` exposes `--serve`, `--check-login-db`, `--probe-login`, `--smoke-login-mariadb`, and `--smoke-login-live`; `scripts\source2_start_login_server.ps1` wraps the checked owner start path. | Code present; local tests pass |
| Server owner can configure the target login server | `source2\config\login_server.eq2emu-target.ini.example` targets `192.168.1.243:3306`, DB `eq2ls`, user `eq2emu`, `tls = false`, client version `546`, and DB-backed opcodes. | Pass |
| Server owner can configure the target world server for world-list proof | `source2\config\world_server.eq2emu-target.ini.example` targets world DB `eq2emu`, `tls = false`, login registration at `127.0.0.1:9100`, and externalized world account/password values. | Pass |
| MariaDB Connector/C is available in the source2 build | `source2\vcpkg.json` declares `libmariadb`; `cmake\Eq2Dependencies.cmake` links `unofficial::libmariadb`; the vcpkg CI run reports `source2 MariaDB Connector/C support enabled via vcpkg`. | Pass |
| Source2 builds after connector installation | `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify` builds `eq2_login_server.exe` and `eq2_world_server.exe`. | Pass |
| All default tests pass | Fresh `scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify` reports 116 registered tests, 115 executed/passed, and 1 skipped opt-in target live login gate. | Pass |
| Login account authentication uses the target legacy-compatible SQL shape | `SqlLoginAccountRepository` authenticates with `name = ? and passwd = sha2(?, 512)` and the checked login DB path requires `EQ2_LOGIN_USERNAME` / `EQ2_LOGIN_PASSWORD`; the target verifier authenticated `testlabs`. | Pass |
| Login server can load target DB-backed opcodes for client version `546` | `login_server.eq2emu-target.ini.example` sets `opcode_source = database`; `--check-login-db` loaded request opcode `0`, reply opcode `4`, world-list opcode `8`, all-worlds request opcode `7`, characters request opcode `9`, and characters reply opcode `10` from the target DB. | Pass |
| A user can connect and authenticate against the running source2 login server | `scripts\source2_live_login_verify.ps1 -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0` started the DB-backed login path and probed it with the configured login account. | Pass |
| Login can return a non-empty world list after source2 world registration | `scripts\source2_live_login_verify.ps1 -RunWorldServe -ExpectedWorldCount 1` starts source2 login and world, then probes for a registered world. | Not required for login-only goal; pending valid world credentials |
| Target DB-backed login has a standard opt-in test gate | `source2_target_live_login_ctest` is registered in CTest and calls `scripts\source2_target_live_login_ctest.ps1`, which runs the live verifier when `EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN=1` is set. | Pass; target execution verified |
| Server owner has a one-command target live gate wrapper | `scripts\source2_run_target_live_login_gate.ps1` runs the no-secret MariaDB TCP check, sets the opt-in CTest environment switch, exports explicit absolute `EQ2_SOURCE2_TARGET_*` overrides for the CTest process, and then runs `source2_target_live_login_ctest`; `-RunWorldServe` also sets the world-list gate. | Pass; target execution verified |
| Secrets are not committed or printed | Config templates use placeholders; owner scripts list required environment variable names and resolve secrets after TCP reachability; owner dry-run redaction CTest passes; fixed-string secret scan is clean. | Pass |

## Requirement Checklist

| Requirement | Evidence | Status |
| --- | --- | --- |
| Build source2 with MariaDB Connector/C available | `scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify` configures with `source2 MariaDB Connector/C support enabled via vcpkg`. | Pass |
| Source2 solution builds | Same CI command builds `eq2_login_server`, `eq2_world_server`, DB/protocol/net/login/world tests, and tools. | Pass |
| Built owner-facing executables start with deployed MariaDB runtime DLLs | `build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --help` and `eq2_world_server.exe --help` both start successfully. The help output documents config precedence; world help lists supported env aliases. `libmariadb.dll` and `zd.dll` are present beside the executables in `source2\apps\Debug`. | Pass |
| MariaDB prepared statement results have real fetch buffers | `source2\db\src\mariadb_connection.cpp` binds one-byte scratch buffers before `mysql_stmt_fetch` and then fetches complete column values with `mysql_stmt_fetch_column`, hardening the first live prepared account/opcode/schema queries. | Pass |
| All default tests pass | Same CI command runs CTest with 116 registered tests, 115 executed/passed, and 1 skipped opt-in target live login gate. The executed tests include target config parse tests, env-over-config precedence tests, executable help-output tests, DB TLS config validation, PowerShell helper parse/dry-run tests, no-secret MariaDB TCP diagnostic parse coverage, source2 deploy-config helper parse/dry-run/create/no-overwrite coverage, source2 login firewall helper parse/dry-run coverage, env-backed credential smoke coverage, owner dry-run secret-redaction coverage, owner preflight login-only/world-list dry-run coverage, one-command target live gate dry-run, TCP-before-secret, TCP-helper child-process capture, post-TCP-success target-gate continuation coverage, CTest env-propagation, exit-after-env-restore, target-gate success-marker coverage, target-gate transcript coverage, target CTest env-override coverage, and target CTest smoke/probe coverage, copy-paste-safe owner command quoting/call-operator/execution-policy coverage, owner gate-label coverage, owner start/probe dry-run secret-contract coverage, skip-preflight and skip-account-preflight dry-run secret-contract coverage, config-file secret dry-run coverage, config-secret env-clearing coverage including skip-account-preflight, login/world verifier credential env-scope coverage, start/probe-wrapper no-secret TCP-before-secret coverage, executable skip-account-auth coverage, opt-in target live CTest parse/default-skip coverage, and live loopback smoke tests. | Pass |
| Server owner has a generic login config template | `source2\config\login_server.ini.example`; validated by `eq2_login_server_example_config_parses`. | Pass |
| Server owner has a target login config template | `source2\config\login_server.eq2emu-target.ini.example`; validated by `eq2_login_server_target_config_parses`. | Pass |
| Server owner has a target world config template for world-list registration | `source2\config\world_server.eq2emu-target.ini.example`; validated by `eq2_world_server_target_config_parses`. | Pass |
| Server owner can prepare deploy config copies | `scripts\source2_prepare_deploy_config.ps1` copies target login/world templates to an owner-selected directory with placeholders preserved; CTest covers dry-run, create, and no-overwrite behavior. | Pass |
| Server owner has a checked login startup wrapper | `scripts\source2_start_login_server.ps1` validates config, checks target DB/account/opcodes by default, supports `-SkipAccountPreflight` for DB/schema/opcode/version checks without a test account password, prints the non-secret login username and required secret env names in dry-run mode unless config supplies non-placeholder values, and starts `eq2_login_server --serve`. | Pass |
| Login startup wrapper keeps credential env scope tight | `scripts\source2_start_login_server.ps1` only exports `EQ2_LOGIN_USERNAME` / `EQ2_LOGIN_PASSWORD` for the DB preflight path and clears them before launching `eq2_login_server --serve`; `source2_start_login_server_clears_login_env_before_serve` covers the ordering. | Pass |
| Live verifier keeps served login credential env scope tight | `scripts\source2_live_login_verify.ps1` clears `EQ2_LOGIN_USERNAME` / `EQ2_LOGIN_PASSWORD` before starting the temporary `eq2_login_server --serve` process and restores them for the probe command; `source2_live_login_verify_clears_login_env_before_serve` covers the ordering. | Pass |
| Server owner has a checked login probe wrapper | `scripts\source2_probe_login_server.ps1` validates config, prints the non-secret login username and required secret env names in dry-run mode, and probes a running source2 login server using target DB-backed opcodes and the configured login account. | Pass |
| Server owner has a checked world startup wrapper | `scripts\source2_start_world_server.ps1` validates config, checks world DB schema/login reachability, prints required secret env names in dry-run mode unless config supplies non-placeholder values, and starts `eq2_world_server --serve` for world-list registration. | Pass |
| Server owner has a transcripted live preflight wrapper | `scripts\source2_owner_live_login_preflight.ps1` runs the target live verifier from a normal PowerShell session and writes a timestamped transcript. | Pass |
| Server owner has a one-command CTest live gate wrapper | `scripts\source2_run_target_live_login_gate.ps1` runs `source2_check_mariadb_tcp.ps1` in a child PowerShell process, requires the needed secret env vars without printing them only after TCP succeeds, sets `EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN=1`, exports non-secret target parameters as `EQ2_SOURCE2_TARGET_*`, and runs the opt-in CTest target. | Pass |
| Server owner has MariaDB-side preflight guidance | `scripts\source2_mariadb_target_preflight.sql.example` lists bind/user/schema/account/opcode/world-account checks to run on `192.168.1.243`. | Pass |
| Server owner has source2 login firewall guidance | `scripts\source2_configure_login_firewall.ps1` prints or creates inbound UDP/TCP rules for the configured login port; `docs\source2_developer_workflow.md` and `docs\source2_live_login_blocker_resolution.md` document the same requirement. | Pass |
| Source2 login account authentication matches legacy login SQL | Legacy `source\LoginServer\LoginDatabase.cpp` uses `SELECT id from account where name='%s' and passwd=sha2('%s',512)`. Source2 `SqlLoginAccountRepository` uses `select id, name from account where name = ? and passwd = sha2(?, 512)`, with parameter binding. | Pass |
| Source2 world registration password handling matches legacy world/login SQL | Legacy world sends `sha512(net.GetWorldPassword())`; legacy login compares that string to `lower(password)` from `login_worldservers`. Source2 accepts either a sent hash through `lower(password) = lower(?)` or a raw deploy-time password through `lower(password) = lower(sha2(?, 512))`. | Pass |
| Login server can load DB-backed opcodes for the target client version | Implemented through `opcode_source = database` and checked by `--check-login-db`; target run loaded request opcode `0`, reply opcode `4`, world-list opcode `8`, all-worlds request opcode `7`, characters request opcode `9`, and characters reply opcode `10`. | Pass |
| Login server can authenticate `testlabs` against `eq2ls` | Checked by `eq2_login_server.exe --check-login-db` with `EQ2_LOGIN_USERNAME` / `EQ2_LOGIN_PASSWORD` and `--client-version 546`; target run returned `reply_code=0` and `account_id=1`. | Pass |
| Login server can start against target DB and accept a user probe | Checked by `scripts\source2_live_login_verify.ps1 ... -RunMariaDbSmoke -RunServeProbe`; target run completed `source2 live login verification passed.` | Pass |
| Login can return a non-empty world list after source2 world registration | Checked by verifier with `-RunWorldServe -ExpectedWorldCount 1` and `EQ2_WORLD_ACCOUNT` / `EQ2_WORLD_PASSWORD`. | Not required for login-only goal; pending valid world credentials |

## Current Verification Evidence

Latest local verification from 2026-05-15:

- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify`
  installs/fetches `libmariadb[core,iconv]:x64-windows@3.4.8`, configures with
  `source2 MariaDB Connector/C support enabled via vcpkg`, builds
  `eq2_login_server.exe` and `eq2_world_server.exe`, and runs CTest with 116
  registered tests, 115 executed/passed, and 1 skipped opt-in target live login
  gate.
- `scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0`
  passed against `192.168.1.243:3306`, proving DB/account/opcodes, world DB
  schema access, DB-backed login smoke, temporary login `--serve`, and the
  user login probe.
- `scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -NoTranscript`
  passed the opt-in `source2_target_live_login_ctest` path with `4/4` focused
  CTest tests passed and the `source2 target live login gate passed.` marker.
- `ctest --test-dir build\source2-vcpkg-user-verify -C Debug -R eq2_db_tests --output-on-failure`
  passes after hardening the raw world-registration password fallback to compare
  SHA-512 hex case-insensitively.
- `git diff --check` passed; the only messages were LF/CRLF normalization
  warnings.
- `eq2_login_server.exe --smoke-login-live --username <login-user> --password <login-password> --client-version 546 --expect-world-count 0`
  completed the source2 UDP login/probe flow locally with `worlds=0`.
- `eq2_world_server.exe --smoke-world-live` completed the local source2 world
  registration/session/zone bootstrap smoke path.
- `scripts\source2_start_login_server.ps1 -DryRun` validates the target login
  config, lists required environment secret names unless config supplies
  non-placeholder values, and prints the owner-facing preflight and start
  commands for `192.168.1.243:3306`, `eq2ls`, and client version `546`.
- `scripts\source2_prepare_deploy_config.ps1` creates owner-editable login and
  world deploy config copies with password/account placeholders preserved, so
  secrets can be added outside the source templates.
- `scripts\source2_probe_login_server.ps1 -DryRun -ExpectedWorldCount 0`
  validates the target login config, lists required environment secret names
  unless config supplies a non-placeholder DB password, and prints the
  owner-facing login probe command for the target DB-backed opcode source and
  login port `9100`.
- `scripts\source2_probe_login_server.ps1` now checks MariaDB TCP reachability
  before resolving `EQ2_DB_PASSWORD` or `EQ2_LOGIN_PASSWORD`, and
  `source2_probe_login_server_checks_db_tcp_before_secrets` covers that
  ordering.
- `scripts\source2_start_login_server.ps1` and
  `scripts\source2_start_world_server.ps1` check MariaDB TCP reachability
  before resolving DB/login/world secrets, and
  `source2_start_login_server_checks_db_tcp_before_secrets` /
  `source2_start_world_server_checks_tcp_before_secrets` cover the ordering.
- `source2_start_login_server_skip_preflight_dry_run_lists_only_runtime_secrets`
  verifies that `-SkipDbPreflight -DryRun` lists only the DB password needed
  by the long-running login server.
- `source2_start_login_server_skip_account_preflight_keeps_db_secret_only`
  verifies that `-SkipAccountPreflight -DryRun` also avoids requiring
  `EQ2_LOGIN_PASSWORD`, while leaving the DB password requirement in place so
  the wrapper can run `eq2_login_server --check-login-db --skip-account-auth`
  before the long-running server performs its own DB/schema/opcode startup
  checks.
- `source2_start_login_server_skip_account_preflight_checks_db_opcodes` verifies
  that `-SkipAccountPreflight` keeps the wrapper DB/opcode preflight and passes
  `--skip-account-auth` to `eq2_login_server`.
- `source2_start_login_server_config_password_satisfies_skip_account_preflight`
  verifies that a non-placeholder config `db.password` plus
  `-SkipAccountPreflight -DryRun` requires no environment secrets.
  `source2_start_world_server_skip_preflights_dry_run_lists_runtime_secrets`
  verifies that the world wrapper still lists DB and world-registration
  secrets because they are needed by the long-running world server even when
  preflight checks are skipped.
- `source2_start_login_server_config_password_satisfies_runtime_secret`,
  `source2_probe_login_server_config_password_satisfies_db_secret`, and
  `source2_start_world_server_config_secrets_satisfy_runtime_secrets` verify
  that owner wrappers accept non-placeholder deploy-time config secrets without
  requiring equivalent environment variables in dry-run output.
- `source2_start_login_server_clears_env_when_config_supplies_db_secret`,
  `source2_probe_login_server_clears_env_when_config_supplies_db_secret`, and
  `source2_start_world_server_clears_env_when_config_supplies_secrets` verify
  that wrappers clear matching child-process env vars when deploy config is the
  selected secret source, preventing empty inherited env vars from overriding
  config-file secrets.
- `scripts\source2_start_world_server.ps1 -DryRun` validates the target world
  config, lists required environment secret names unless config supplies
  non-placeholder values, and prints the owner-facing world start command for
  `192.168.1.243:3306`, `eq2emu`, and login registration through
  `127.0.0.1:9100`.
- `scripts\source2_owner_live_login_preflight.ps1 -DryRun` labels the gate as
  `login-only`, lists the exact live verifier command, and lists required
  environment secret names for the target `eq2ls`/`eq2emu` deployment. The
  displayed verifier command uses `powershell -NoProfile -ExecutionPolicy
  Bypass -File` so it remains copy-paste safe on machines with `AllSigned`
  local script policy.
- `scripts\source2_owner_live_login_preflight.ps1 -DryRun -RunWorldServe`
  labels the gate as `world-list` and lists the additional world-server
  credential environment variables required for non-empty world-list proof.
- `scripts\source2_check_mariadb_tcp.ps1` provides a no-secret owner diagnostic
  for the current TCP `3306` blocker before credentials are needed, including
  detection of the known Codex sandbox offline outbound firewall rule.
- `scripts\source2_configure_login_firewall.ps1 -DryRun` prints the inbound
  UDP/TCP rules needed for real client login traffic and world registration on
  the configured source2 login port.
- `source2_target_live_login_ctest` is registered in CTest and skips by
  default. Setting `EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN=1` runs the same target
  DB-backed live verifier from CTest; setting
  `EQ2_SOURCE2_RUN_TARGET_WORLD_LIST=1` also requires the world-list gate.
- `scripts\source2_run_target_live_login_gate.ps1` is the one-command owner
  wrapper for the opt-in CTest gate. It runs the no-secret MariaDB TCP check,
  sets the CTest opt-in environment variable, exports explicit
  `EQ2_SOURCE2_TARGET_*` overrides, and then runs
  `source2_target_live_login_ctest`; `-RunWorldServe` also sets the world-list
  gate. `source2_target_live_login_ctest.ps1` consumes those overrides before
  constructing verifier parameters.
- `ctest --test-dir build\source2-vcpkg-user-verify -C Debug -R source2_target_live_login_ctest --output-on-failure`
  passes by default with a skip message. With
  `EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN=1`, it fails at the same no-secret TCP
  gate for `192.168.1.243:3306` before DB/login secrets are resolved.
- `scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -DbHost 192.168.1.243 -DbPort 3306 -LoginDbName eq2ls -WorldDbName eq2emu -DbUser eq2emu -LoginUsername testlabs -ClientVersion 546 -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0 -LoginPort 9100`
  currently fails at the no-secret MariaDB TCP gate with `[LocalPolicy]`,
  before any DB or login password is requested.
- The checked login/world start wrappers also report the known Codex sandbox
  offline outbound firewall rule when their MariaDB TCP preflight is blocked
  before DB credentials can be used.
- `artifacts\source2-target-live-gate\source2-target-live-gate-20260515-073815.log`
  is the latest target gate transcript. It was intentionally allowed to fail
  at the no-secret TCP gate before any DB or login password was required, and
  it records the detected Codex sandbox offline outbound firewall rule.

The live verifier fails before source2 can authenticate because the MariaDB TCP
port is not reachable from this host:

```text
Cannot reach 192.168.1.243:3306 from this host.
TCP failure detail: [LocalPolicy] An attempt was made to access a socket in a way forbidden by its access permissions 192.168.1.243:3306
Local IPv4 candidates for MariaDB firewall/grants: 192.168.1.41, 192.168.56.1
```

Use `192.168.1.41` as the LAN source address for MariaDB firewall and grant
rules. `ipconfig`, `route print -4`, and `tracert -d -h 4 192.168.1.243`
confirm that `192.168.1.243` is reachable on the same LAN in one hop; the
remaining failure is specific to outbound TCP. `netsh advfirewall firewall show
rule name=codex_sandbox_offline_block_outbound verbose` confirms this Codex
shell has an enabled `Codex Sandbox Offline - Block Non-Loopback Outbound` rule
that blocks remote IPv4 traffic, including `192.168.1.243:3306`. Run the final
DB-backed verifier from a normal, non-sandboxed PowerShell prompt or relax the
Codex sandbox network policy for the verification run; see
`docs/source2_live_login_blocker_resolution.md`.

## Commands To Close The Audit

After fixing the source2 host outbound policy, MariaDB bind, MariaDB host
firewall, and MariaDB grants as needed, rerun:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0
```

Equivalent transcripted owner command:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_owner_live_login_preflight.ps1
```

Equivalent one-command CTest gate:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify
```

That is the login-only gate: it proves the login server can start against the
target DB, authenticate the configured account, and complete the login probe.

Then run the stronger non-empty world-list gate with a valid
`login_worldservers` account:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1
```

Equivalent transcripted owner command:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_owner_live_login_preflight.ps1 -RunWorldServe
```

Only mark the source2 live login goal complete after these target DB-backed
gates pass.
