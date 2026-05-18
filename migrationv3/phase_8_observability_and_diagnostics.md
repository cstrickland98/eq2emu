# Phase 8: Observability And Diagnostics

Status: blocked.

## Purpose

Make source2 login-server parity diagnosable without relying on packet guessing
or external packet captures for every failure.

## Where Work Begins

Start after Phase 7 is complete, but add urgent diagnostic hooks earlier if a
phase is blocked by missing visibility.

## Work Entailed

- Add structured login event logging for:
  - transport connect/disconnect,
  - session request/response,
  - key request and RC4 state transitions,
  - app opcode dispatch,
  - auth outcomes,
  - world registration outcomes,
  - character lifecycle outcomes,
  - unsupported/malformed packet reasons.
- Add optional packet trace output with redaction controls for credentials and
  secrets.
- Add a live-client diagnostic mode that prints event summaries while serving.
- Add counters for active clients, registered worlds, login attempts, failures,
  malformed packets, retransmits, and unsupported opcodes.
- Add troubleshooting documentation that maps symptoms to source2 events.

## Deliverables

- Diagnostic logging/tracing implementation.
- Updated login server guide troubleshooting section.
- Tests for redaction and event formatting.

## Exit Criteria

- A real-client failed login attempt can be diagnosed from source2 logs without
  Wireshark for the common failure classes.
- Packet tracing is opt-in and does not log passwords by default.
- Existing scripts can collect diagnostics for support.

## Progress

- Added redacted live-login event formatting for transport, LoginStream,
  application opcode, auth, world registration, character lifecycle, client log,
  malformed, unsupported, and disconnect events.
- Added live-login diagnostic counters for event count, active client sessions,
  registered worlds, login attempts, login failures, malformed packets, and
  unsupported packets.
- Added `--diagnostic-events`, `login.diagnostic_events`, and
  `EQ2_LOGIN_DIAGNOSTIC_EVENTS` to print live event summaries while serving.
- Diagnostic event formatting is tested to include useful opcode/version context
  without credential values.
- Updated `docs/source2_login_server_guide.md` with a troubleshooting table that
  maps common symptoms to redacted diagnostic events:
  `transport_accepted`, `session_requested`, `key_requested`, `login_accepted`,
  `login_rejected`, `world_registered`, `world_status_updated`,
  `character_create_rejected`, `character_rejected`, `play_rejected`,
  `client_log_rejected`, `malformed`, and `unsupported`.
- Ran an automated failed-login diagnostic gate against a running source2 login
  server with `--diagnostic-events`. The probe used the target DB and account
  name with an intentionally wrong password; the probe failed with
  `reply_code=1`, the server emitted `login_rejected`, and the diagnostic
  output did not include the bad password.
- Diagnostic gate artifacts:
  - `artifacts\source2-diagnostic-gate\failed-login-diagnostic-stdout.log`
  - `artifacts\source2-diagnostic-gate\failed-login-diagnostic-stderr.log`
- Verified current Phase 8 sensitive tests and source2 CI:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_db_tests eq2_login_server_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\db\Debug\eq2_db_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-15: source2 CI reported 0 failures out of 116 tests;
the opt-in target live-login CTest was skipped by design.

Remaining before completion:

- Run a real-client failed-login attempt with `--diagnostic-events` and confirm
  the failure can be diagnosed from source2 logs without Wireshark. The
  automated failed-login diagnostic gate passes; the remaining check is the
  real client.
- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 scripts docs migrationv3
git commit -m "Add login parity diagnostics"
```
