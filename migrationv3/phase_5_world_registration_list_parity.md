# Phase 5: World Registration And World List Parity

Status: blocked.

## Purpose

Match original login-server behavior for world server registration, status,
world-list serialization, and world update flows.

## Where Work Begins

Start after Phase 4 is complete. Use:

- `source/LoginServer/LWorld.cpp`
- `source/LoginServer/LWorld.h`
- `source/LoginServer/LoginDatabase.cpp`
- `source/LoginServer/client.cpp`
- `source/common/servertalk.h`
- Source2 world registration and login world-list code.

## Work Entailed

- Match world TCP registration packet parsing and authentication.
- Match invalid account, disabled account, bad version, and ghost-world cleanup
  behavior.
- Match world address, port, status, admin level, locked/hidden flags, and
  placeholder behavior.
- Match world-list packet serialization for supported client versions.
- Match server-list update packets sent after world changes.
- Match world stat persistence, connected-time reset, old stat cleanup, and
  console world-list output.
- Match login-to-world support data needed by play-character requests.
- Match zone update and equipment appearance update requests/responses handled
  by the login server.

## Deliverables

- Source2 world registration parity implementation.
- DB repository coverage for original world account, status, stats, zone, and
  equipment update operations.
- Tests for registration accept/reject, ghost cleanup, world-list generation,
  and status update behavior.
- Live check with a real or characterized world server registering with source2.

## Exit Criteria

- A legacy-compatible world server can register with source2 login and appear in
  the real client world list.
- World-list bytes for supported client versions match characterized original
  output.
- World status/stat/update behavior is either matched or explicitly reviewed as
  obsolete.

## Progress

- Existing coverage already accepted legacy `ServerOP_LSInfo` registration and
  world-list generation for the live UDP login path.
- Added `ServerOP_LSStatus` payload encode/decode coverage for the original
  packed status shape:
  - status,
  - current players,
  - current zones,
  - world max level.
- Source2 live login now tracks authenticated world TCP sessions after
  `ServerOP_LSInfo`, consumes authenticated keepalives, handles
  `ServerOP_LSStatus`, and rejects interserver packets sent before LSInfo.
- Registered world status now feeds the client world list:
  - `status == -2` becomes locked,
  - player count is serialized into the legacy 546 world-list payload.
- SQL-backed world status persistence now mirrors the original
  `UpdateWorldServerStats` behavior at the repository boundary:
  - upsert `login_worldstats`,
  - update `login_worldservers.lastseen`.
- Source2 now sends the legacy world-support update requests after
  `ServerOP_LSInfo` registration:
  - `ServerOP_ZoneUpdates` with the original max batch of 20,
  - `ServerOP_LoginEquipment` with the original max batch of 100.
- Source2 now decodes compressed interserver payloads before dispatching them,
  matching the legacy TCP layer's auto-inflate behavior.
- Added repository and live-login handling for `ServerOP_ZoneUpdates` so world
  zone names/descriptions persist into `ls_world_zones`.
- Added repository and live-login handling for `ServerOP_LoginEquipment` so
  world equipment appearance updates persist into `login_equipment`.
- Added duplicate world-session cleanup: when a world account registers again,
  older sessions for that same world id are removed and the old TCP socket is
  closed.
- World registration and status changes now broadcast a refreshed world list to
  already logged-in source2 clients.
- Source2 world registration now keeps the login TCP connection open after
  `ServerOP_LSInfo`, matching the original long-lived world/login connection.
  This prevents source2 login from registering the world and immediately
  removing it on disconnect before a client requests the world list.
- Fixed the target live verifier wrapper so `Start-Process` preserves argument
  values with spaces, including the default world name.
- Verified current world-registration/list-sensitive tests:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_login_server_tests eq2_world_registration_characterization_tests eq2_db_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_world_registration_characterization_tests.exe
build\source2-vcpkg-user-verify\source2\db\Debug\eq2_db_tests.exe
```

- Verified target DB-backed source2 login/world serve and probe with one
  advertised world:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1 -LoginPort 9100 -WorldPort 9101
```

Observed result on 2026-05-15:

```text
login-db-check reply_code=0 account_id=1 ... key_request_opcode=2
world-db-check ok database=eq2emu
smoke-login-mariadb reply_code=0 account_id=1 world_list=yes worlds=0 character_list=yes post_world_reply=yes
login probe reply_code=0 account_id=1 world_list=yes worlds=1 character_list=yes post_world_reply=yes
source2 live login verification passed.
```

Remaining before completion:

- Live check with a source2 or legacy-compatible world server registering and
  appearing in the real client world list. The automated source2 world-list
  gate passes; the remaining check is the real client.
- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_world_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\world_server.eq2emu-target.ini
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\login_server.eq2emu-target.ini -ConnectHost 127.0.0.1 -ConnectPort 9100 -ExpectedWorldCount 1
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 scripts docs migrationv3
git commit -m "Match legacy world registration and list behavior"
```
