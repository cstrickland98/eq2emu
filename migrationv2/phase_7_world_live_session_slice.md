# Phase 7: World Live Session Slice

Status: blocked.

## Purpose

Prove that source2 world can participate in a real login-to-world flow:
register with login, accept a client/session handoff, load character data,
select a character, and request zone admission through a boundary.

Implementation is complete and covered by local tests. The phase remains
blocked for final target readiness in this Codex shell by the
`codex_sandbox_offline_block_outbound` firewall rule. The target DB gate must
be rerun from a normal shell or a network-enabled Codex session, and a valid
`login_worldservers` account/password is still required for non-empty
world-list verification.

## Where Work Begins

Start only after Phase 6 login live slice is complete. Use:

- Production-shaped `eq2_world_server` app.
- Real net transport from Phase 2.
- Protocol pipeline from Phase 3.
- MariaDB repositories from Phase 4.
- Source2 login from Phase 6.
- World session boundary from v1 Phase 8.

## Work Entailed

- Wire world app to real config, logging, net, protocol, and DB services.
- Implement source2 world-to-login connection and `ServerOP_LSInfo` registration over real transport.
- Implement login-to-world handoff request/response over protocol framing.
- Validate access keys and account IDs.
- Load character list from real DB repositories.
- Preserve known legacy character select mapping behavior.
- Add a world session model that owns authenticated session state.
- Keep zone interaction behind `ZoneHandoff`.
- Add live smoke coverage for:
  - world registration,
  - character list,
  - character select,
  - handoff request creation.

## Deliverables

- Source2 world app using real services.
- World registration integration tests.
- Character list/select integration tests against real or isolated DB.
- Login-to-world smoke script or command.
- Compatibility notes for existing clients and legacy login/world mixed modes.

## Exit Criteria

- Source2 world registers with source2 login over real transport.
- Character list for a known account matches characterized legacy mapping.
- Character select creates a valid zone handoff request.
- World code still does not directly own or mutate zone internals.
- Temporary live-client incompatibilities are documented with missing packet/support work.

## Progress

- Extended `LiveLoginService` so source2 login can receive world `ServerOP_LSInfo` registration frames over real TCP when a world-registration repository is supplied.
- Added `eq2/world/live_world.h`, a production-shaped live world slice that:
  - starts real loopback TCP transport for client intake,
  - runs inbound client bytes through `StreamPipeline`,
  - sends `ServerOP_LSInfo` registration to the configured source2 login address and port over real TCP,
  - creates and validates login-to-world handoff access keys,
  - opens authenticated world sessions only after access-key validation,
  - loads character lists through `CharacterListRepository`,
  - preserves characterized character select mapping,
  - keeps zone admission behind `ZoneHandoff`.
- Added `eq2_world_server --smoke-world-live`.
  - The smoke starts a source2 login receiver.
  - The world registers with that login over a real socket.
  - The smoke admits a login handoff, validates the access key, loads a SQL-backed character list, selects a character, and requests zone admission.
- Added `eq2_world_server --serve`.
  - The owner-facing app can now start the source2 world TCP listener, connect to the configured login server, and send legacy `ServerOP_LSInfo`.
  - Serve validates required world registration fields (`world.name`, `world.account`, `world.password`, and `world.server_version`) before attempting DB or login connections.
  - The example world config now includes the world registration fields, a separate client-facing `world.advertised_address`, and the server/database version fields used by `LSInfo`.
- Expanded `eq2_world_session_tests` with a live integration test for:
  - source2 login startup,
  - source2 world startup,
  - world-to-login LSInfo over TCP,
  - configured login address usage during world registration,
  - SQL-backed login world registration,
  - real client protocol bytes reaching the world stream pipeline,
  - TCP session response bytes written back to the connected client,
  - bad access-key rejection,
  - accepted access-key session open,
  - SQL-backed character list,
  - character select,
  - login handoff response frame serialization,
  - play-character response payload serialization,
  - zone handoff request creation.

Compatibility notes:

- Live MariaDB execution is supported through vcpkg `libmariadb`; target DB validation still requires TCP reachability between this source2 host and the MariaDB service.
- Latest vcpkg-backed local verification starts the built
  `eq2_world_server.exe` with its deployed MariaDB runtime DLLs and passes
  `eq2_world_server.exe --smoke-world-live`.
- World registration now dials the configured `login_address`/`login_port`; tests cover that it no longer forces loopback.
- The source2 world serve path is enough to advertise a registered source2 world to source2 login, but live character play remains staged.
- World client packet coverage is still staged. Session response writes and play-character response serialization are covered; character-list app dispatch and full live-client opcode routing remain feature-driven work.
- Mixed legacy login/world compatibility is not complete. Source2 login and source2 world can register and hand off through source2 protocol structures; legacy mixed-mode sessions still need live compatibility testing.

## Blocking Target-DB Gate

Before this phase can be marked complete for the requested deployment target,
the following command must pass from the source2 host with credentials from a
valid `login_worldservers` row:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1
```

This gate proves source2 login can start against `eq2ls`, source2 world can
start against `eq2emu`, the world can register with login over TCP, and a user
login probe receives the expected non-empty world list.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```

Verification run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```
