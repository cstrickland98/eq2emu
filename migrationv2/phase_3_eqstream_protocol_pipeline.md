# Phase 3: EQStream Protocol Pipeline

Status: complete.

## Purpose

Connect source2 protocol helpers to real net transport through an EQ2-compatible stream/session pipeline. This phase should prove that source2 can parse and emit real client-facing session bytes, not only standalone packet fixtures.

## Where Work Begins

Start with:

- `source2/protocol/include/eq2/protocol/session.h`
- `source2/protocol/include/eq2/protocol/protocol_packet.h`
- `source2/protocol/include/eq2/protocol/application_packet.h`
- `source2/protocol/include/eq2/protocol/packet_transform.h`
- `source2/protocol/include/eq2/protocol/combined_packet.h`
- `source2/net` real transport from Phase 2.
- Legacy references:
  - `source/common/EQStream.*`
  - `source/common/EQPacket.*`
  - `source/common/Crypto.*`
  - `source/common/CRC16.*`
  - `source/common/RC4.*`

## Work Entailed

- Define a source2 stream pipeline that owns:
  - session request/response,
  - sequencing/ACK boundaries,
  - protocol packet decode,
  - app packet extraction,
  - combined packet handling,
  - compression/encryption boundary calls,
  - CRC policy.
- Decide whether compression/encryption/CRC are implemented now or adapter-wrapped behind interfaces first.
- Add a packet handler interface consumed by login/world services.
- Add tests with inline fixtures and, where possible, captured packets.
- Add an integration test over loopback UDP using the real net transport.
- Preserve protocol module dependency isolation.
- Document unsupported legacy stream behaviors explicitly.

## Deliverables

- EQ2 stream/session pipeline type.
- Packet dispatch boundary from net bytes to protocol/app packets.
- Encode boundary for outbound protocol/app packets.
- Tests for session handshake, app packet dispatch, combined packets, CRC policy, and malformed packet rejection.
- Notes on compression/encryption implementation status.

## Exit Criteria

- Source2 can process a real or loopback EQ2 session request and produce the expected session response.
- Source2 can decode at least one login app packet through the full net-to-protocol pipeline.
- Protocol tests remain independent of login/world/zone/db/scripting.
- Unsupported stream features are documented before live login work begins.

## Progress

- Added `source2/protocol/include/eq2/protocol/stream_pipeline.h`.
- Added `source2/protocol/include/eq2/protocol/crc.h` with the legacy keyed
  CRC used by `EQStream`.
- Added `eq2::protocol::StreamPipeline` with:
  - session request handling,
  - session response generation,
  - minimal sequenced `OP_Packet` app dispatch and ACK emission,
  - sequenced outbound app packet wrapping,
  - outbound CRC attachment for non-session protocol packets,
  - inbound CRC stripping when a valid client CRC is present,
  - app packet decode/dispatch events,
  - combined-packet recursion,
  - keepalive/disconnect/malformed/unsupported event classification.
- Kept stream pipeline independent of login, world, zone, DB, scripting, and net.
- Extended `eq2_protocol_tests` for full stream handshake and app-packet dispatch over byte fixtures.
- Added `eq2_stream_loopback_tests`, a separate integration test target linking `eq2::protocol` and `eq2::net`, to prove UDP loopback bytes flow through the stream pipeline.
- Unsupported stream features still intentionally sit behind existing Phase 4 protocol transform boundaries: full compression, encryption, fragment reassembly, reliable resend windows, and out-of-order queues remain separate implementation work before full production client compatibility. CRC is implemented for the login path.
- Verification:
  - `cmake -S . -B build\source2`: passed.
  - `cmake --build build\source2 --config Debug --target eq2_protocol_tests eq2_stream_loopback_tests`: passed.
  - `ctest --test-dir build\source2 -C Debug -R "eq2_protocol_tests|eq2_stream_loopback_tests" --output-on-failure`: passed.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_protocol_tests eq2_net_tests
ctest --test-dir build\source2 -C Debug -R "protocol|net|stream" --output-on-failure
```
