# Phase 6: Login Live Vertical Slice

Status: complete.

## Purpose

Prove that source2 login can handle the real login path through typed config, real net transport, source2 protocol pipeline, and real DB repositories.

## Where Work Begins

Start only after Phases 1 through 4 are complete. Use:

- Production-shaped `eq2_login_server` app.
- Real net transport from Phase 2.
- EQStream/protocol pipeline from Phase 3.
- MariaDB repositories from Phase 4.
- Characterization tests from v1 Phase 2.

## Work Entailed

- Wire login app to real config, logging, net, protocol, and DB services.
- Implement login packet handler over the full net/protocol pipeline.
- Preserve legacy reply-code behavior.
- Implement account creation policy if configured.
- Implement duplicate-session behavior or document temporary limitation.
- Implement world-list response boundary if enough protocol support exists; otherwise document a staged compatibility gap.
- Add a live smoke test mode that uses real service wiring and an isolated test DB.
- Test bad client version, invalid password, account creation, malformed packet, and graceful disconnect.
- Confirm whether a legacy world server can still register with source2 login or document incompatibility.

## Deliverables

- Source2 login app running with real transport and DB repositories.
- Full-pipeline login tests.
- Live smoke instructions.
- Compatibility notes for legacy world server and existing clients.
- Logs for startup, successful login, rejection, and shutdown paths.

## Exit Criteria

- A client or protocol smoke harness can connect to source2 login over real transport.
- Source2 login reads account data from a real DB repository.
- Successful and failed login outcomes match characterized legacy behavior.
- Temporary incompatibilities are documented with concrete missing work.
- Default app path no longer uses fake repositories.

## Progress

- Added `eq2/login/live_login.h`, a production-shaped live login slice that:
  - starts real loopback TCP transport through `TcpSocketServer`,
  - feeds received bytes through `eq2::protocol::StreamPipeline`,
  - handles login application packets by opcode,
  - authenticates through the `LoginAccountRepository` boundary,
  - supports account creation according to login config,
  - preserves characterized legacy reply codes for accepted login, invalid credentials, and bad client version,
  - tracks duplicate account sessions and flags the existing-session disconnect behavior,
  - captures protocol reply packets at the boundary for later transport write support,
  - records malformed protocol bytes and protocol disconnect events.
- Added `eq2_login_server --smoke-login-live`.
  - The smoke mode starts the live login service on an ephemeral loopback TCP port.
  - It sends a protocol-framed login request through a real socket.
  - It uses `SqlLoginAccountRepository` over a test `QueryConnection`, not fake repositories.
  - It exits successfully only when the live slice accepts the SQL-backed test account.
- Expanded `eq2_login_server_tests` with full-pipeline live tests for:
  - successful login over real TCP,
  - SQL repository parameter usage,
  - duplicate session flagging,
  - invalid password rejection,
  - bad client version rejection,
  - account creation when configured,
  - malformed protocol bytes,
  - graceful disconnect.

Compatibility notes:

- Live MariaDB execution is still limited by the Phase 4 connector dependency. Phase 6 uses the real SQL repository implementation over a test `QueryConnection`; it does not claim a MariaDB-backed login smoke.
- The current real socket transport emits inbound events but does not yet write captured protocol replies back to the connected socket. Login reply packets are encoded and captured at the service boundary for the next transport-write step.
- World-list response encoding is still a staged compatibility gap. Accepted login currently marks `should_send_world_list_after_login`.
- Legacy world registration remains covered by LSInfo parsing and repository characterization tests, but a legacy world server cannot yet complete a real interserver TCP registration session against the live socket path.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_login_server eq2_login_server_tests
ctest --test-dir build\source2 -C Debug -R "login" --output-on-failure
build\source2\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
```

Verification run:

```powershell
cmake --build build\source2 --config Debug --target eq2_login_server eq2_login_server_tests
ctest --test-dir build\source2 -C Debug -R "login" --output-on-failure
build\source2\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
```
