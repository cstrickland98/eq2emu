# Source2 / Original Packet And Diagnostic Comparison

Status: partial.

## Inputs

- Pre-fix source2 real-client capture:
  `E:\_EQ2\packets\source2hex.txt`
- Original source real-client capture:
  `E:\_EQ2\packets\sourcehex.txt`
- Source2 pcapng:
  `E:\_EQ2\packets\source2.pcapng`
- Original source pcapng:
  `E:\_EQ2\packets\source.pcapng`
- Packet directory scan on 2026-05-15:
  `E:\_EQ2\packets` contained only the existing `source2hex.txt`,
  `source2.pcapng`, `sourcehex.txt`, and `source.pcapng` captures from the
  earlier source2 failure and original success runs. No newer post-fix
  real-client capture was present there.
- Target source2 DB-backed verifier after the migration fixes:

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

## Pre-Fix Capture Summary

The pre-fix source2 capture contains 11 UDP packets: 6 client-to-server and 5
server-to-client. The original source capture contains 23 UDP packets: 12
client-to-server and 11 server-to-client.

The real client reached both servers and both servers completed the initial
session request / session response path. The first meaningful historical
divergence was the sequenced 113-byte client packet:

```text
0009 0000 0019 44 ...
```

That packet is an `OP_Packet` carrying an embedded encrypted
`OP_AppCombined` flow:

- subpacket 1: RSA/RC4 key material,
- subpacket 2: encrypted `OP_LoginRequestMsg`.

The original server ACKed that packet and then emitted the next encrypted
post-login sequenced response. The pre-fix source2 server only ACKed the
113-byte packet and did not continue into the encrypted login request contained
in the same embedded combined packet.

## Migration Fixes Covering The Divergence

The migration addressed that historical divergence with these changes:

- `StreamPipeline` processes embedded `OP_AppCombined` subpackets inside a
  sequenced `OP_Packet` instead of treating the whole combined payload as key
  material.
- The encrypted login subpacket is decrypted and dispatched in the same
  datagram after the key subpacket.
- The decrypted 546 `LS_LoginRequest` payload is parsed using the original
  classic layout: session strings, username/password, two 32-bit numeric
  fields, and the 16-bit client version. Source2 previously required unused
  trailing fields after the version and rejected the real client payload.
- Reliable outbound packets are retained so `OP_OutOfOrderAck` can resend the
  requested sequence.
- Session reconnect resets encryption, sequencing, queued future packets, and
  fragments.
- Source2 world registration keeps the login TCP connection open after
  `ServerOP_LSInfo`, so registered worlds remain visible to world-list probes.
- The target probe accepts real non-empty `OP_AllCharactersDescReplyMsg`
  packets instead of assuming the target account has zero characters.

## Current Comparison Status

The automated DB-backed source2 gate now proves the fixed path without a real
client:

- target DB connection succeeds,
- login account authentication succeeds,
- DB-backed opcode loading reports `key_request_opcode=2`,
- MariaDB-backed post-login smoke receives world-list, character-list, and
  post-world-description replies,
- a temporary source2 world registers with source2 login,
- the source2 probe receives a world list containing one world,
- the source2 probe receives a character-list reply.

This is strong automated evidence, but it is not a replacement for a fresh
post-fix real-client packet comparison. The old source2 capture is explicitly a
failing pre-fix trace.

## Captured Source2 Replay After Parser Fix

The exact source2 client UDP payloads from `E:\_EQ2\packets\source2hex.txt`
were replayed against the patched DB-backed source2 login server on
2026-05-16:

```text
artifacts\source2-captured-client-replay\20260516-015711
```

The replay now records:

- session request/response,
- legacy session-disconnect reply,
- key request with `OP_SessionStatResponse` and `OP_WSLoginRequestMsg`,
- ACK for the encrypted key/login `OP_AppCombined` packet,
- encrypted sequenced login reply,
- `login_accepted`,
- `login_attempts=1`,
- `malformed_packets=0`,
- `unsupported_packets=0`.

This verifies the historical packet-level divergence is closed for the captured
login request. It is still not the final Phase 9 UI acceptance evidence because
it does not exercise the live client world-list/character/play screens.

## Outbound Coalescing Parity Follow-Up

The original successful capture also showed the legacy login server coalescing
small pending protocol replies into one `OP_Combined` UDP datagram. The earlier
source2 capture emitted the equivalent key/stat and app replies as separate
datagrams. Source2 now has a bounded outbound coalescing path for small
CRC-protected UDP protocol response batches, and the probe/smoke decoders now
accept CRC-wrapped `OP_Combined` replies.

Verification on 2026-05-16:

```text
ctest --test-dir build\source2-vcpkg-user-verify -C Debug --output-on-failure
```

Result: 155/155 source2 CTests passed, with the opt-in target live-login CTest
skipped by design.

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1 -WorldAdvertisedAddress 192.168.1.41 -NoTranscript
```

Result: 4/4 matching target live-login CTest tests passed, including the
DB-backed login smoke, temporary source2 login/world serve, source2 probe, and
one expected registered world.

## Remaining Final-Acceptance Comparison

Before Phase 9 can be marked complete, collect a fresh real-client source2
capture after the current fixes and compare it with the original source capture
for the target deployment:

- client progresses past `Trying login server #1`,
- source2 emits the encrypted post-login response after the embedded
  key+login `OP_AppCombined` packet,
- real client receives a non-empty world list when a world is registered,
- real client receives the expected character list,
- create/delete/play packet and diagnostic paths either match original behavior
  or are explicitly listed as approved differences.
