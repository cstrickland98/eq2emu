# Source2 Live Login Completion Audit

Last updated: 2026-05-15.

## Completion Decision

Status: complete.

The source2 code path, owner wrappers, connector-enabled build, local test
suite, direct target verifier, and opt-in target CTest gate are in place and
green. The login-only target gate passed against `192.168.1.243:3306` using the
provided login account, proving that a user can authenticate against a running
source2 login server.

Target parameters under audit:

- Login DB host: `192.168.1.243:3306`
- Login DB name: `eq2ls`
- World DB name: `eq2emu`
- DB user: `eq2emu`
- Login account: `testlabs`
- Login client version: `546`

Passwords are intentionally not recorded in this audit or committed config
templates.

## Success Criteria

- A server owner can configure source2 login for the target MariaDB login DB.
- A server owner can start the source2 login server with checked DB/schema,
  opcode, and account preflight behavior.
- A user can connect to the running source2 login server and authenticate with
  the configured login account.
- The source2 build uses MariaDB Connector/C.
- The solution builds and all default tests pass.
- Target DB-backed login proof has a repeatable closeout command.
- Secrets are not committed or printed.

## Prompt-To-Artifact Checklist

| Requirement | Artifact or command evidence | Audit state |
| --- | --- | --- |
| Target login DB is configurable | `source2\config\login_server.eq2emu-target.ini.example` uses host `192.168.1.243`, port `3306`, DB `eq2ls`, user `eq2emu`, `tls = false`, client version `546`, and `opcode_source = database`. | Verified |
| Target world DB is configurable for world-list proof | `source2\config\world_server.eq2emu-target.ini.example` uses host `192.168.1.243`, port `3306`, DB `eq2emu`, `tls = false`, and externalized world account/password values. | Verified |
| Target login account is the default live-gate account | `scripts\source2_run_target_live_login_gate.ps1` defaults `-LoginUsername` to `testlabs`; callers can override it explicitly. | Present |
| Deploy config copies can be prepared without committing secrets | `scripts\source2_prepare_deploy_config.ps1` copies the target login/world config templates to an owner-selected output directory with placeholders preserved. | Verified locally |
| MariaDB Connector/C is part of source2 build | `source2\vcpkg.json` declares `libmariadb`; `scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify` reports `source2 MariaDB Connector/C support enabled via vcpkg`. | Verified locally |
| Source2 solution builds | The same CI command builds `eq2_login_server.exe`, `eq2_world_server.exe`, source2 libraries, and tests. | Verified locally |
| All default tests pass | The same CI command runs CTest with `116` registered tests, `115` executed/passed, and `1` skipped opt-in target live login gate. | Verified locally |
| Login server can be owner-started with checked preflight | `scripts\source2_start_login_server.ps1 -DryRun` validates config, prints the non-secret login username, lists required secret env names, prints the planned `--check-login-db` preflight command, and prints the planned `--serve` command. | Verified locally |
| Login preflight can skip account auth without skipping DB/opcodes | `scripts\source2_start_login_server.ps1 -SkipAccountPreflight -DryRun` prints `--check-login-db --skip-account-auth`; CTest covers the same dry-run contract. | Verified locally |
| User probe path exists | `scripts\source2_probe_login_server.ps1` validates config, checks DB TCP before secrets, scopes DB/login credential env vars, and runs `eq2_login_server --probe-login`. | Verified locally |
| Live login verifier covers target DB-backed behavior | `scripts\source2_live_login_verify.ps1 -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0` checks TCP, DB/account/opcodes, world DB schema, DB-backed login smoke, temporary `--serve`, and `--probe-login`. | Verified against target |
| CTest has an opt-in target live login gate | `source2_target_live_login_ctest` is registered and skipped by default; `EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN=1` runs the live verifier. | Verified against target |
| Owner has one command to run the target gate | `scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify` runs the no-secret TCP helper in a child PowerShell process first, requires secrets only after TCP succeeds, sets CTest env overrides, resolves relative build dirs to absolute paths for the CTest subprocess, and runs the opt-in CTest gate. | Verified against target |
| Target gate has an unambiguous pass marker and transcript | `scripts\source2_run_target_live_login_gate.ps1` prints `source2 target live login gate passed.` only after CTest returns exit code `0`; non-dry target runs write a transcript under `artifacts\source2-target-live-gate`; CTest statically covers both behaviors. | Verified locally |
| Server owner can open the login port for real clients | `scripts\source2_configure_login_firewall.ps1 -DryRun` prints inbound UDP/TCP firewall rules for the configured login port; running it elevated without `-DryRun` creates missing rules. | Implemented locally |
| Owner dry-runs do not reveal secret values | `source2_owner_wrappers_dry_run_redact_env_secret_values` runs the owner dry-run wrappers with canary secret environment values and fails if any value is printed. | Verified locally |
| Secrets are not committed | Config templates contain placeholders; docs use placeholder secret names; fixed-string secret scan for the supplied login password literal is clean. | Verified locally |

## Target Evidence

Latest direct target verifier:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0
```

Result:

```text
login-db-check reply_code=0 account_id=1 request_opcode=0 reply_opcode=4 world_list_opcode=8 all_worlds_request_opcode=7 characters_request_opcode=9 characters_reply_opcode=10
world-db-check ok database=eq2emu
smoke-login-mariadb reply_code=0 account_id=1 world_list=yes worlds=0 character_list=yes post_world_reply=yes
login probe reply_code=0 account_id=1 world_list=yes worlds=0 character_list=yes post_world_reply=yes
source2 live login verification passed.
```

Latest opt-in target CTest gate:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -NoTranscript
```

Result:

```text
ctest --test-dir E:\_EQ2\eq2emu\build\source2-vcpkg-user-verify -C Debug -R source2_target_live_login_ctest --output-on-failure
100% tests passed, 0 tests failed out of 4
source2 target live login gate passed.
```

Latest focused owner/start/probe coverage:

```powershell
ctest --test-dir build\source2-vcpkg-user-verify -C Debug -R "(source2_start_login_server_|source2_probe_login_server_|source2_live_login_verify_|eq2_login_live_smoke)" --output-on-failure
```

Result:

```text
22/22 tests passed
```

Latest direct owner dry-run verification:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath source2\config\login_server.eq2emu-target.ini.example -RunForMs 1 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath source2\config\login_server.eq2emu-target.ini.example -ConnectHost 127.0.0.1 -ConnectPort 9100 -ExpectedWorldCount 0 -DryRun
```

Result:

```text
Both commands validated the target login config, printed the non-secret login
username testlabs, listed required secret env var names only, and printed the
planned target DB-backed preflight/serve/probe commands for 192.168.1.243:3306,
eq2ls, DB user eq2emu, and client version 546.
```

## Repeatable Login-Only Gate

Run the login-only gate from a PowerShell prompt:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify
```

This gate proves that source2 can connect to the target MariaDB login DB, load
DB-backed opcodes for client version `546`, authenticate the configured login
account, start the DB-backed login server, and complete the user login probe.

The stronger world-list gate requires a valid `login_worldservers`
account/password:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe
```

The detailed readiness evidence and blocker-resolution steps are maintained in
`docs\source2_live_login_readiness_audit.md` and
`docs\source2_live_login_blocker_resolution.md`.
