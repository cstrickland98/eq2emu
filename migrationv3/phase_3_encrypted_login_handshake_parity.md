# Phase 3: Encrypted Login Handshake Parity

Status: blocked.

## Purpose

Match the original login server's RSA/RC4 negotiation and encrypted LoginStream
application packet framing.

## Where Work Begins

Start after Phase 2 is complete. Use:

- `source/common/Crypto.*`
- `source/common/RC4.*`
- `source/common/EQStream.cpp`
- `source/common/EQPacket.cpp`
- `source/LoginServer/client.cpp`
- Source2 protocol/login packet serializers.

## Work Entailed

- Match `OP_ServerKeyRequest` and `OP_SessionStatResponse` behavior.
- Match `OP_WSLoginRequestMsg` payload and framing.
- Extract the RC4 key from the same combined subpacket as the original server.
- Continue processing additional embedded subpackets in the same datagram after
  key extraction.
- Match encrypted inbound app opcode parsing for one-byte and extended opcode
  forms.
- Match encrypted outbound app packet preparation, including legacy padding,
  sequence, opcode, compression flag, and CRC behavior.
- Add replay tests using the captured 113-byte real-client packet that contains
  both RSA key material and the encrypted login request.

## Deliverables

- RC4/key-negotiation parity implementation.
- Encrypted inbound and outbound packet tests.
- Real-client replay that reaches decrypted `OP_LoginRequestMsg`.
- Packet evidence that source2 emits the same next server packet class that the
  original server emits after the client's RSA/login combined packet.

## Exit Criteria

- The real EQ2 client progresses past `Trying login server #1` to receive a
  source2 login reply.
- Packet replay proves source2 handles embedded key+login app-combined packets.
- Source2 encrypted outbound login replies match the legacy framing expected by
  the client.
- Source2 probe remains green.

## Progress

- Added embedded combined key extraction that uses the same subpacket boundary
  as the original `OP_AppCombined` loop before continuing to the encrypted login
  subpacket.
- Added RC4 initialization guard for malformed zero-key tails.
- Added encrypted outbound one-byte opcode preparation so legacy replies decrypt
  with the expected leading zero padding before the one-byte application opcode.
- Added protocol replay coverage proving the captured client packet decrypts to
  `OP_LoginRequestMsg` with the expected username/password bytes.
- Replayed the exact pre-fix source2 real-client UDP payloads against the
  patched DB-backed login server. Before the parser fix the replay reached the
  encrypted login packet but logged `malformed login request payload`; after the
  fix it produced `login_accepted`, `login_attempts=1`,
  `malformed_packets=0`, and an encrypted sequenced login reply.
- Added live UDP login-service coverage that performs:
  - session request/response,
  - `OP_ServerKeyRequest` / `OP_SessionStatResponse`,
  - source2 `OP_WSLoginRequestMsg` key request,
  - client-shaped sequenced embedded `OP_AppCombined`,
  - encrypted login request decryption,
  - encrypted login reply decryption.
- Verified current handshake-sensitive tests:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_login_server_tests --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --check-login-db --skip-account-auth --config artifacts\source2-deploy-config\login_server.eq2emu-target.ini
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed results:

- `smoke-login-live accepted account=42 worlds=0`
- `login-db-check account_auth=skipped request_opcode=0 reply_opcode=4 world_list_opcode=8 all_worlds_request_opcode=7 characters_request_opcode=9 characters_reply_opcode=10 key_request_opcode=2`
- Captured source2 UDP replay artifact:
  `artifacts\source2-captured-client-replay\20260516-015711`.
- Source2 CI: 0 failures out of 116 tests; the opt-in target live login CTest
  was skipped by design.

Remaining before completion:

- Manual real-client validation that the client progresses past
  `Trying login server #1` against this build.
- Source2 probe rerun against the running target service after the user rebuilds
  and starts the patched binary.
- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\login_server.eq2emu-target.ini -ConnectHost 127.0.0.1 -ConnectPort 9100 -ExpectedWorldCount 0
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 scripts docs migrationv3
git commit -m "Match legacy encrypted login handshake"
```
