# Phase 7: World Live Session Slice

Status: complete.

## Purpose

Prove that source2 world can participate in a real login-to-world flow: register with login, accept a client/session handoff, load character data, select a character, and request zone admission through a boundary.

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
  - sends `ServerOP_LSInfo` registration to source2 login over real TCP,
  - creates and validates login-to-world handoff access keys,
  - opens authenticated world sessions only after access-key validation,
  - loads character lists through `CharacterListRepository`,
  - preserves characterized character select mapping,
  - keeps zone admission behind `ZoneHandoff`.
- Added `eq2_world_server --smoke-world-live`.
  - The smoke starts a source2 login receiver.
  - The world registers with that login over a real socket.
  - The smoke admits a login handoff, validates the access key, loads a SQL-backed character list, selects a character, and requests zone admission.
- Expanded `eq2_world_session_tests` with a live integration test for:
  - source2 login startup,
  - source2 world startup,
  - world-to-login LSInfo over TCP,
  - SQL-backed login world registration,
  - real client protocol bytes reaching the world stream pipeline,
  - bad access-key rejection,
  - accepted access-key session open,
  - SQL-backed character list,
  - character select,
  - zone handoff request creation.

Compatibility notes:

- Live MariaDB execution remains blocked on the Phase 4 connector dependency. Phase 7 uses SQL repository implementations over isolated test `QueryConnection` objects.
- The real socket helper currently targets loopback only, so the smoke validates source2 transport wiring but not remote host dialing through `world.login_address`.
- World client packet coverage is still staged. The live world service records stream-pipeline app packets, but character-list and play-character response encoding are not yet sent to a real client socket.
- Mixed legacy login/world compatibility is not complete. Source2 login and source2 world can register and hand off through source2 protocol structures; legacy mixed-mode sessions still need packet-response and socket-write work.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_login_server eq2_world_server eq2_world_session_tests
ctest --test-dir build\source2 -C Debug -R "world|character|login" --output-on-failure
build\source2\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```

Verification run:

```powershell
cmake -S . -B build\source2
cmake --build build\source2 --config Debug --target eq2_login_server eq2_world_server eq2_world_session_tests
ctest --test-dir build\source2 -C Debug -R "world|character|login" --output-on-failure
build\source2\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```
