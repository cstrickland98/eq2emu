# Phase 6: Character Lifecycle Parity

Status: blocked.

## Purpose

Match original login-server character list, create, delete, play, and
login-to-world handoff behavior.

## Where Work Begins

Start after Phase 5 is complete. Use:

- `source/LoginServer/client.cpp`
- `source/LoginServer/PacketHeaders.cpp`
- `source/LoginServer/Character.*`
- `source/LoginServer/LoginDatabase.cpp`
- `server/LoginStructs.xml`
- Source2 character list and handoff code.

## Work Entailed

- Match character-list account info and character profile serialization for
  supported client versions.
- Match login character loading from `login_characters`, equipment, colors,
  appearance, picture, and zone metadata tables.
- Match `OP_AllCharactersDescRequestMsg` behavior.
- Match `OP_CreateCharacterRequestMsg`, world forwarding, approval, rejection,
  DB save, and optional immediate play behavior.
- Match `OP_DeleteCharacterRequestMsg`, pending delete, world forwarding, DB
  verification, and response behavior.
- Match `OP_PlayCharacterRequestMsg`, server lookup, access-key generation,
  world request forwarding, and play response behavior.
- Match failure paths for invalid world, invalid character, world down, and
  rejected play/create/delete responses.

## Deliverables

- Character select/list serializers matching original structs.
- DB repository methods for original character lifecycle reads/writes.
- World interserver forwarding support for create/delete/play.
- Tests and packet fixtures for character list, create, delete, and play paths.

## Exit Criteria

- The real client can log in, see the same character list as original login,
  create/delete where supported, and receive a play response that points at the
  selected world.
- Character lifecycle DB effects match the original server for covered paths.
- Failure responses match original behavior.

## Progress

- Existing protocol characterization covers legacy play-character request and
  response layouts plus delete-character request and response layouts.
- Existing source2 login path responds to post-login character-list requests
  with a valid empty character-list payload.
- Added a repository boundary for the original
  `LoginDatabase::VerifyDelete` behavior:
  - SQL marks `login_characters.deleted = 1` by account, character id, server
    id, and name,
  - fake repository removes the character from the account list for tests.
- Added source2 live-login handling for `OP_DeleteCharacterRequestMsg`:
  - rejects delete before login,
  - parses the legacy delete request,
  - emits `OP_DeleteCharacterReplyMsg`,
  - refreshes `OP_AllCharactersDescReplyMsg` after the delete, matching the
    original `Client::Process` branch.
- Added legacy world delete forwarding:
  - encodes `ServerOP_BasicCharUpdate`,
  - sends `CharDataUpdate_Struct` with `DELETE_UPDATE_FLAG` and update data `1`
    to the registered world when login DB deletion succeeds.
- Added world-to-login character metadata update handling:
  - `ServerOP_CharTimeStamp` updates `login_characters.unix_timestamp`,
  - `ServerOP_BasicCharUpdate` updates level, class, gender, and world-side
    delete state,
  - `ServerOP_RaceUpdate` updates race/model type,
  - `ServerOP_NameCharUpdate` updates character name,
  - `ServerOP_ZoneUpdate` updates current zone id.
- Added interserver TCP frame buffering/splitting so bursts of world packets
  that arrive coalesced in one TCP receive are processed as individual legacy
  server packets.
- Added live TCP test coverage for login -> delete character -> world
  `BasicCharUpdate` forward -> delete reply -> character-list refresh.
- Added non-empty character-list loading and serialization backed by
  `login_characters`, login-world, zone, appearance-color, and equipment rows.
- Added play-character request handling that validates the selected character,
  forwards `ServerOP_UsertoWorldReq` to the registered world session, tracks the
  pending request, and returns `OP_PlayCharacterReplyMsg` after the world
  response.
- Added source2 protocol helpers for the original create-character packet
  exchange:
  - parse supported `CreateCharacter` request headers,
  - prepend the client version and patch `account_id` at the same legacy
    offsets used by `source/LoginServer/client.cpp`,
  - decode the world `ServerOP_CharacterCreate` response,
  - encode `LS_CreateCharacterReply` for legacy and newer reply layouts.
- Added live-login handling for `OP_CreateCharacterRequestMsg`:
  - rejects create before login or for unavailable worlds,
  - forwards the patched create payload to the registered world over
    `ServerOP_CharacterCreate`,
  - tracks the pending client create request,
  - handles accepted/rejected world responses,
  - emits `OP_CreateCharacterReplyMsg`,
  - saves the created login-character row through the repository boundary,
  - refreshes the character list after accepted creates,
  - sends the original login server's immediate play handoff for 546/561
    clients.
- Added fake and SQL repository coverage for saving created login-character
  rows. The SQL path deactivates duplicate world character ids before inserting
  the new `login_characters` row.
- Added config, CLI, environment, and database opcode loading for
  create/delete/play request and reply opcodes.
- Added live-login test coverage for registered-world non-empty character lists
  and login -> play request -> world response -> play reply.
- Added protocol and live-login test coverage for login -> create request ->
  world create response -> create reply -> list refresh -> legacy automatic
  play handoff.
- Added live-login test coverage for registered world metadata updates and
  protocol tests for the packed interserver update payloads.
- Added source2 protocol and repository handling for the original character
  select support updates:
  - `ServerOP_LoginEquipment` decodes `EquipmentUpdateList_Struct` and stores
    rows in `login_equipment`,
  - `ServerOP_CharacterPicture` decodes `CharPictureUpdate_Struct` and stores
    the hex-encoded image in `ls_character_picture`,
  - `ServerOP_ZoneUpdates` stores world zone names/descriptions used by
    character-list zone text.
- Added live-login coverage proving registered-world equipment, picture, and
  world-zone update packets reach the fake repository and mutate the character
  select data source.
- Added legacy `CreateCharacter` appearance persistence for the supported
  546/561 create flow:
  - source2 parses the original legacy appearance block, appearance file names,
    body size, body age, and signed float color/shape triplets,
  - SQL save now inserts the original `login_characters` appearance-id columns,
    resolves appearance ids by name through the login `appearances` table,
    deactivates duplicate world character ids after the insert, and writes
    `login_char_colors` rows for the parsed color/shape data,
  - fake repository save now exposes the created character's body values and
    color rows to character-select tests.
- Completed the play-handoff access-key parity review. The original login
  server copies `UsertoWorldResponse_Struct.access_key` into
  `LS_PlayResponse.access_code` on accepted world responses; source2 now has
  protocol and live-login coverage for the same world-provided access code.
- Verified current character-lifecycle-sensitive tests and source2 CI:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_login_server_tests eq2_db_tests eq2_protocol_tests --config Debug
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
build\source2-vcpkg-user-verify\source2\db\Debug\eq2_db_tests.exe
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-15 before the create-character patch: source2 CI
reported 0 failures out of 116 tests; the opt-in target live-login CTest was
skipped by design.

Focused verification after the create-character patch:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_db_tests eq2_login_server_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\db\Debug\eq2_db_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
```

Observed result on 2026-05-15: all focused commands passed.

Full verification after the create-character patch:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-15: source2 CI reported 0 failures out of 116 tests;
the opt-in target live-login CTest was skipped by design.

Full verification after world metadata update handling:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-15: source2 CI reported 0 failures out of 116 tests;
the opt-in target live-login CTest was skipped by design.

Focused verification after the legacy `SaveCharacter` appearance/color patch:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_db_tests eq2_login_server_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\db\Debug\eq2_db_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
```

Observed result on 2026-05-15: all focused commands passed.

Full verification after the legacy `SaveCharacter` appearance/color patch:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-15: source2 CI reported 0 failures out of 116 tests;
the opt-in target live-login CTest was skipped by design.

Target DB-backed login/world probe after the persistent world-registration and
character-list probe fixes:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1 -LoginPort 9100 -WorldPort 9101
```

Observed result on 2026-05-15:

```text
smoke-login-mariadb reply_code=0 account_id=1 world_list=yes worlds=0 character_list=yes post_world_reply=yes
login probe reply_code=0 account_id=1 world_list=yes worlds=1 character_list=yes post_world_reply=yes
source2 live login verification passed.
```

The probe now accepts non-empty target character-list replies by recognizing
the `OP_AllCharactersDescReplyMsg` app opcode and validating the known trailer
when present, instead of assuming the target account always has zero visible
characters.

Remaining before completion:

- Live check with the real client character select, create/delete, and play
  flows.
- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Manual live gate:

```text
Log in with the real client, inspect the character list, create/delete a test
character on a test world, and play an existing character far enough to receive
the world handoff response.
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 scripts docs migrationv3
git commit -m "Match legacy character lifecycle login behavior"
```
