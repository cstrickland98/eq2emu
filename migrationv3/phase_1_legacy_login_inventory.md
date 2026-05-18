# Phase 1: Legacy Login Inventory

Status: blocked.

## Purpose

Inventory every behavior handled by the original login server and map each
scenario to source2 implementation, test, and live verification work.

## Where Work Begins

Use:

- `source/LoginServer/client.cpp`
- `source/LoginServer/net.cpp`
- `source/LoginServer/LWorld.cpp`
- `source/LoginServer/LoginDatabase.cpp`
- `source/LoginServer/PacketHeaders.cpp`
- `source/common/EQStream.cpp`
- `source/common/EQPacket.cpp`
- `server/LoginStructs.xml`
- `source/common/login_oplist.h`
- Current source2 login, protocol, net, and DB modules.

## Work Entailed

- Build a scenario matrix covering:
  - client session/connect/disconnect/reconnect,
  - login accept/reject/bad version/account creation/duplicate session,
  - world registration/auth/status/list/stat cleanup,
  - character list/create/delete/play,
  - client logs,
  - web/console/status operations,
  - DB writes and maintenance jobs.
- For each scenario, record:
  - original source entry point,
  - protocol opcode and struct name,
  - DB tables touched,
  - source2 status,
  - required tests,
  - required live verification.
- Mark any behavior that is intentionally obsolete only after explicit review.

## Deliverables

- Scenario matrix added under `Progress` or in a linked file inside
  `migrationv3`.
- Gap list sorted by login-client impact.
- Test fixture list for packet replay and DB characterization.

## Exit Criteria

- Every original `Client::Process` opcode branch is accounted for.
- Every original `LWorld` registration/world-list/status path is accounted for.
- Every original LoginStream transport behavior used by login clients is
  accounted for.
- No phase after Phase 1 contains undefined parity scope.

## Progress

Local phase evidence is complete, but the phase cannot be closed until its git
commit is made.

- Added `legacy_scenario_inventory.md` with scenario tables for:
  - LoginStream transport,
  - encrypted login handshake,
  - account login policy,
  - world registration and world list,
  - character lifecycle,
  - client logs, admin, web, and maintenance.
- Accounted for the original `Client::Process` login opcodes used by the login
  server:
  - `OP_LoginRequestMsg`,
  - `OP_KeymapLoadMsg`,
  - `OP_AllWSDescRequestMsg`,
  - `OP_LsClientCrashlogReplyMsg`,
  - `OP_LsClientVerifylogReplyMsg`,
  - `OP_LsClientAlertlogReplyMsg`,
  - `OP_LsClientBaselogReplyMsg`,
  - `OP_AllCharactersDescRequestMsg`,
  - `OP_CreateCharacterRequestMsg`,
  - `OP_PlayCharacterRequestMsg`,
  - `OP_DeleteCharacterRequestMsg`.
- Accounted for original `LWorld` and `LWorldList` world registration,
  authentication, ghost cleanup, world-list, status/stat, zone update, and
  equipment appearance update flows.
- Accounted for the LoginStream transport behavior that source2 must match
  before application-level parity can be accepted.
- Split the remaining implementation scope across Phases 2 through 9 so later
  phases have a bounded parity target.

## Verification Commands

```powershell
rg -n "case OP_|ServerOP_|void Client::|bool Client::|void LWorld|bool LWorld|void LWorldList|bool LWorldList" source/LoginServer source/common/EQStream.cpp
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add migrationv3
git commit -m "Inventory legacy login server parity scope"
```
