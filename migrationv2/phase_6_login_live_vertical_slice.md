# Phase 6: Login Live Vertical Slice

Status: blocked.

## Purpose

Prove that source2 login can handle the real login path through typed config,
real net transport, source2 protocol pipeline, and real DB repositories.

Implementation is complete and covered by local tests. The phase remains
blocked for final live readiness in this Codex shell because the
`codex_sandbox_offline_block_outbound` firewall rule blocks non-loopback
outbound traffic. The target `eq2ls` DB, `testlabs` account, and DB-backed
serve/probe gate must be verified from a normal shell or from a Codex session
with network policy relaxed for `192.168.1.243:3306`.

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
  - starts TCP and UDP listeners on the configured login port, matching the legacy split where clients use UDP and world/interserver registration uses TCP,
  - feeds received bytes through `eq2::protocol::StreamPipeline`,
  - handles sequenced client `OP_Packet` login requests and writes sequenced login/world-list responses after session establishment,
  - emits legacy CRCs on non-session protocol replies so real LoginStream clients can validate source2 responses,
  - handles login application packets by configurable opcode and opcode width,
  - authenticates through the `LoginAccountRepository` boundary,
  - supports account creation according to login config,
  - preserves characterized legacy reply codes for accepted login, invalid credentials, and bad client version,
  - serializes client version 546 login replies with the legacy `LS_LoginReplyMsg` 284-era layout from `server/LoginStructs.xml`,
  - serializes client version 546 world lists with the legacy `LS_WorldList` layout from `server/LoginStructs.xml`,
  - handles the legacy post-login `OP_AllWSDescRequestMsg` request,
  - handles explicit legacy `OP_AllCharactersDescRequestMsg` requests for clients that ask for the character list separately,
  - sends a minimal legacy empty character-list response through `OP_AllCharactersDescReplyMsg` so clients can progress without migrated character data,
  - tracks duplicate account sessions and flags the existing-session disconnect behavior,
  - captures protocol reply packets at the boundary for later transport write support,
  - records malformed protocol bytes and protocol disconnect events.
- Added `eq2_login_server --smoke-login-live`.
  - The smoke mode starts the live login service on an ephemeral loopback TCP port.
  - It sends a protocol-framed login request through a real socket.
  - It uses `SqlLoginAccountRepository` over a test `QueryConnection`, not fake repositories.
  - It exits successfully only when the live slice accepts the SQL-backed test account.
- Added login opcode configuration through INI, environment, and CLI:
  - `[login] opcode_source`, `opcode_width`, `request_opcode`, `reply_opcode`, `world_list_opcode`, `all_worlds_request_opcode`, `characters_request_opcode`, and `characters_reply_opcode`,
  - DB-backed lookup of `OP_LoginRequestMsg`, `OP_LoginReplyMsg`, `OP_WorldListMsg`, `OP_AllWSDescRequestMsg`, `OP_AllCharactersDescRequestMsg`, and `OP_AllCharactersDescReplyMsg` from the login `opcodes` table when `opcode_source = database`,
  - `--login-opcode-source`, `--login-opcode-width`, `--login-request-opcode`, `--login-reply-opcode`, `--login-world-list-opcode`, `--login-all-worlds-request-opcode`, `--login-characters-request-opcode`, and `--login-characters-reply-opcode`,
  - `EQ2_LOGIN_OPCODE_SOURCE`, `EQ2_LOGIN_OPCODE_WIDTH`, `EQ2_LOGIN_REQUEST_OPCODE`, `EQ2_LOGIN_REPLY_OPCODE`, `EQ2_LOGIN_WORLD_LIST_OPCODE`, `EQ2_LOGIN_ALL_WORLDS_REQUEST_OPCODE`, `EQ2_LOGIN_CHARACTERS_REQUEST_OPCODE`, and `EQ2_LOGIN_CHARACTERS_REPLY_OPCODE`.
- Added `[login] client_version` and `EQ2_LOGIN_CLIENT_VERSION` so owner config can select the client build/opcode range for `--serve`, `--check-login-db`, `--probe-login`, and `--smoke-login-mariadb`; `--client-version` remains the CLI override.
- Added `--expect-world-count` to the probe/smoke path so live verification can fail when login succeeds but the world list is unexpectedly empty.
- MariaDB-backed login commands now load the accepted client-version range from the matching legacy `opcodes.version_range1`/`version_range2` rows instead of relying only on the source2 fixture range. This keeps `--serve`, `--check-login-db`, and `--smoke-login-mariadb` aligned with the opcode set selected by `--client-version`.
- Fixed live pipeline construction so one-byte LoginStream application opcodes are decoded on inbound client packets, not only used for outbound serialization.
- Expanded `eq2_login_server_tests` with full-pipeline live tests for:
  - successful login over real TCP,
  - successful login over real UDP,
  - configured one-byte LoginStream opcodes,
  - legacy world `LSInfo` registration over real TCP followed by UDP login receiving a world-list response,
  - TCP login reply bytes written back to the connected client,
  - SQL repository parameter usage,
  - duplicate session flagging,
  - invalid password rejection,
  - bad client version rejection,
  - account creation when configured,
  - malformed protocol bytes,
  - graceful disconnect.

Compatibility notes:

- Live MariaDB execution is supported through vcpkg `libmariadb`; MariaDB-backed login smoke still requires a reachable configured DB and test account.
- The real socket transport now writes source2 protocol replies back to connected TCP and UDP clients and still captures outbound packets for assertions.
- Login reply and 546 world-list response payload serialization now match the legacy login structs used by the configured classic client range.
- Character-list response support is intentionally minimal: source2 sends a valid empty list with account info, but feature migration still needs real character loading/serialization before character selection and play are complete.
- The source2 SQL character repository now reads the legacy `login_characters.char_id` column as `character_id` and filters `deleted = 0`, matching the v1 login DB schema.
- `--check-login-db` now preflights the legacy login character-list support tables: `login_characters`, `login_equipment`, `login_char_colors`, and `ls_world_zones`.
- Legacy world `LSInfo` registration is covered through the live TCP socket path; the registered world is included in the subsequent UDP login world-list response.
- Live DB verification against `192.168.1.243:3306` is currently blocked outside source2: the host responds to ping, but the current Codex shell has an enabled `codex_sandbox_offline_block_outbound` firewall rule that blocks non-loopback outbound traffic, so TCP 3306 cannot be reached from this shell.
- Latest vcpkg-backed local verification builds with MariaDB Connector/C enabled,
  starts the built `eq2_login_server.exe` with its deployed `libmariadb.dll`,
  and passes `eq2_login_server.exe --smoke-login-live --username <login-user>
  --password <login-password> --client-version 546 --expect-world-count 0`.

## Blocking Target-DB Gate

Before this phase can be marked complete for the requested deployment target,
the following command must pass from the source2 host:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0
```

This gate verifies MariaDB TCP reachability, login DB schema, opcode rows for
client version `546`, opcode version ranges, `testlabs` authentication with the
configured login password, world DB schema, DB-backed login smoke, and a real
`--serve`/`--probe-login` run.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-live --username <login-user> --password <login-password> --client-version 546 --expect-world-count 0
```

Verification run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-live --username <login-user> --password <login-password> --client-version 546 --expect-world-count 0
```
