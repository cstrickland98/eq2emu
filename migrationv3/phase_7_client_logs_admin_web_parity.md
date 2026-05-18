# Phase 7: Client Logs, Admin, And Web Parity

Status: blocked.

## Purpose

Match original login-server non-login-flow behavior that operators and clients
still depend on.

## Where Work Begins

Start after Phase 6 is complete. Use:

- `source/LoginServer/client.cpp`
- `source/LoginServer/net.cpp`
- `source/LoginServer/Web/LoginWeb.cpp`
- `source/LoginServer/LoginDatabase.cpp`
- Source2 app config/logging and tools code.

## Work Entailed

- Match client crashlog, verifylog, alertlog, and baselog packet handling.
- Match client log storage semantics and version-dependent parsing.
- Match startup config loading for login address, port, DB, account creation,
  web server, certificates, and log policy.
- Match console commands that list worlds, print version, and stop the server.
- Match web status endpoint behavior and world-list JSON/XML output where still
  used.
- Match periodic maintenance:
  - world stats update,
  - old world stats cleanup,
  - deleted character data cleanup,
  - DB ping/reconnect behavior.

## Deliverables

- Source2 handlers for legacy client log opcodes.
- Source2 operational config/web/console parity or explicitly reviewed
  replacements.
- Tests for log parsing/storage and maintenance jobs.
- Documentation for operator-visible differences, if any are approved.

## Exit Criteria

- Client log packets no longer produce unsupported-opcode behavior.
- Operator commands and status outputs needed by the original login deployment
  are available in source2 or replaced by reviewed equivalents.
- Periodic DB/world maintenance jobs match original behavior where still needed.

## Progress

- Added zlib-backed parsing for legacy compressed client log payloads.
- Source2 live login now handles and records:
  - `OP_LsClientCrashlogReplyMsg` as `Crash Log`,
  - `OP_LsClientVerifylogReplyMsg` as `Verify Log`,
  - `OP_LsClientAlertlogReplyMsg` as `Alert Log`,
  - `OP_LsClientBaselogReplyMsg` as `Base Log`.
- Added DB storage parity for `log_messages(type, message, name, version)`.
- Added config, CLI, environment, and database opcode loading for the four
  client log opcodes.
- Added login maintenance repository coverage for the original cleanup jobs:
  removing old `login_worldstats`, deleting color/equipment rows for deleted
  characters, and running the bug-report percent-decoding fix.
- `--serve` now runs those maintenance jobs every five minutes while the login
  server is active.
- Reviewed the original optional login web routes:
  - `/status` exposed JSON for web/login status, uptime, world count, and
    client count,
  - `/worlds` exposed JSON for registered world id/name/status/player count/IP.
- Source2 does not start a login web server. The reviewed operator replacement
  is the existing source2 CLI/script surface:
  - `--check-login-db` for DB/schema/opcode readiness,
  - `source2_probe_login_server.ps1` for live login/world-count status,
  - `--diagnostic-events` / `EQ2_LOGIN_DIAGNOSTIC_EVENTS` for redacted live
    event summaries and shutdown counters.
- Documented the web endpoint replacement in
  `docs/source2_login_server_guide.md`.
- Verified current Phase 7 sensitive tests:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_db_tests eq2_login_server_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\db\Debug\eq2_db_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-15: all listed commands passed; source2 CI reported
0 failures out of 116 tests and skipped the opt-in target live-login CTest by
design.

Remaining before completion:

- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 scripts docs migrationv3
git commit -m "Match legacy login admin and client log behavior"
```
