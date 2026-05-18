# Phase 2: LoginStream Transport Parity

Status: blocked.

## Purpose

Make source2 LoginStream transport behavior match the original `EQStream`
behavior before application-level login logic is judged.

## Where Work Begins

Start after Phase 1 is complete. Use:

- `source/common/EQStream.cpp`
- `source/common/EQStreamFactory.cpp`
- `source/common/EQPacket.cpp`
- `source2/protocol/include/eq2/protocol/stream_pipeline.h`
- Source2 net transport and stream loopback tests.

## Work Entailed

- Match session request/response behavior, including reconnect attempts and
  out-of-session handling.
- Match CRC behavior for all LoginStream protocol packet types.
- Match ACK, out-of-order ACK, duplicate packet, future packet, and resend
  behavior.
- Match non-sequenced and sequenced outbound queues.
- Match keepalive echo behavior.
- Match `OP_Combined`, embedded protocol packets, `OP_AppCombined`, and
  fragment behavior.
- Add packet-replay tests for real client LoginStream sequences.
- Keep source2 probe compatibility while expanding it to cover real-client
  packet shapes.

## Deliverables

- Source2 protocol transport parity implementation.
- Characterization tests comparing source2 output with original packet captures.
- Replay tests for session, keepalive, ACK, combined, app-combined, fragment,
  duplicate, and reconnect paths.

## Exit Criteria

- Source2 accepts and responds to the same LoginStream protocol packet shapes as
  the original login server for supported client versions.
- Packet-replay tests cover the first source2/original divergence from Phase 0.
- Existing source2 probe and smoke tests still pass.

## Progress

- Added source2 protocol handling for a sequenced `OP_Packet` whose payload
  starts with embedded `00 19` `OP_AppCombined`, matching the original
  `EQStream::HandleEmbeddedPacket` path.
- Source2 now ACKs the outer sequence once and dispatches combined subpackets
  in order instead of treating the entire packet as a standalone RSA key blob.
- Added packet replay coverage for the captured real-client 113-byte
  key+login datagram from `E:\_EQ2\packets\source2hex.txt` and
  `E:\_EQ2\packets\sourcehex.txt`.
- Added parity handling and focused tests for duplicate sequenced packets
  emitting `OP_OutOfOrderAck` without duplicate application dispatch, future
  sequenced packets queueing until the missing sequence arrives, and fragmented
  application packets reassembling before application dispatch.
- Added legacy resend and reconnect coverage:
  - source2 retains reliable outbound `OP_Packet` frames until the matching
    client `OP_Ack`,
  - a client `OP_OutOfOrderAck` for a retained outbound sequence causes the
    same encoded packet bytes to be resent,
  - a new `OP_SessionRequest` resets inbound/outbound sequence state,
    encryption state, queued future packets, and partial fragments before
    accepting the new session.
- Added legacy `OP_SessionDisconnect` reply parity: source2 now responds with
  the same session id and the original server's `0x0006` disconnect reason
  before clearing stream state. The protocol regression test covers the
  client `0x000e` disconnect shape seen in the original/source2 captures.
- Added outbound UDP coalescing parity for small CRC-protected protocol
  replies. When one client datagram causes multiple source2 protocol responses,
  source2 now attempts to emit one legacy `OP_Combined` datagram, matching the
  original stream writer's behavior for ACK/stat/application reply batches.
  The coalescing path is constrained to the same client session/transport so
  TCP world-forward frames are not captured into the client reply batch.
- Updated source2 probe/smoke decoders to accept CRC-wrapped `OP_Combined`
  replies and process all matching app replies inside one combined datagram.
- Verified current transport-sensitive tests and source2 CI:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_login_server eq2_world_server --config Debug
ctest --test-dir build\source2-vcpkg-user-verify -C Debug --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1 -WorldAdvertisedAddress 192.168.1.41 -NoTranscript
```

Observed result on 2026-05-16: full source2 CTest reported 0 failures out of
155 tests with the opt-in target live-login CTest skipped by design. The
DB-backed target live-login/world-list gate then passed 4/4 matching target
CTest tests with a registered source2 world.

Focused verification after the resend/reconnect transport patch:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_stream_loopback_tests --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_stream_loopback_tests.exe
```

Observed result on 2026-05-15: all focused commands passed.

Full verification after the resend/reconnect transport patch:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-15: source2 CI reported 0 failures out of 116 tests;
the opt-in target live-login CTest was skipped by design.

Focused verification after the session-disconnect reply patch:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_login_server_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
```

Observed result on 2026-05-16: all focused commands passed.

Full verification after the session-disconnect reply patch:

```text
ctest --test-dir build\source2-vcpkg-user-verify -C Debug --output-on-failure
```

Observed result on 2026-05-16: 151/151 tests passed; the opt-in target
live-login CTest was skipped by design.

Remaining before completion:

- Run the real-client probe against a rebuilt/running source2 login server.
- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
cmake --build build\source2-vcpkg-user-verify --target eq2_protocol_tests eq2_stream_loopback_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_protocol_tests.exe
build\source2-vcpkg-user-verify\source2\protocol\Debug\eq2_stream_loopback_tests.exe
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 migrationv3
git commit -m "Bring LoginStream transport behavior toward legacy parity"
```
