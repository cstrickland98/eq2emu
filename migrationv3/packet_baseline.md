# Login Packet Baseline

Captured files:

- Source2 failing client run: `E:\_EQ2\packets\source2hex.txt`
- Original source successful client run: `E:\_EQ2\packets\sourcehex.txt`
- Source2 capture: `E:\_EQ2\packets\source2.pcapng`
- Original source capture: `E:\_EQ2\packets\source.pcapng`

## Confirmed Non-Issues

- The real client reaches source2 over UDP `9100`.
- Source2 replies to `OP_SessionRequest`.
- Source2 replies to `OP_ServerKeyRequest`.
- Source2 DB opcode lookup includes `OP_WSLoginRequestMsg`:

```text
request_opcode=0 reply_opcode=4 world_list_opcode=8 all_worlds_request_opcode=7 characters_request_opcode=9 characters_reply_opcode=10 key_request_opcode=2
```

## Source2 Failing Sequence

Key source2 payloads:

```text
client -> source2: 0001000000025352f60f00000200
source2 -> client: 00025352f60f336247020200000000020000000000
client -> source2: 000752d7000000000000000000000000000000000000000000000000000000020000000000000001f83c
source2 -> client: 000852d70b4452f10000000000000000000000000000000000000000000000000000000000000000421e
source2 -> client: 00090000023c000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff01000000015ed8
client -> source2: 00090000001944ffffffff3c00000051a894fa218823d4cff902c37c55ad51fa05a2caac209c212d152221c36b518358cafb0d653e10bd3f94b3eb89b581c54307b80984f3c1efb0f87bfd239d86fd74d5fdbf8d5750ed519e9c301a4875a605a0cfdac83c9e110859454a028df195f840
client -> source2: 001500009e7f
source2 -> client: 001500009e7f
```

Source2 stops after ACKing the 113-byte client packet.

## Original Source Successful Sequence

Comparable original payloads:

```text
client -> source: 0001000000023f83be0100000200
source -> client: 00023f83be01336247020200000000020000000000
client -> source: 0007666c0000000000000000000000000000000000000000000000000000000200000000000000018832
source -> client: 0003280008666c00029ac900000000000000020000000000000001000000000000000100000000000000024a00090000023c000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff0100000001a9e5
client -> source: 00090000001944ffffffff3c00000067b8043cf0bcfb3e4ab2985ac4370681fee7cc37e71d3e62446c5855df8e2291510eaf140ce15d96e3e350eedfd77b56b0c82ea61c2e9b505da1df0623a9b42488e295e19129292da490099b8b4a1e86c1270ed026b6d721e3b71307e8dfcd04ecdb
client -> source: 001500009e7f
source -> client: 001500009e7f
source -> client: 00090001a107972a4609bffda0f683223bdb6de7e1ba3d8fc466e0b2b0486a0a8bbfc27e9da68452229c9907
client -> source: 0009000154cb34fa805d3082e53ee8f8f523d0ec917af24d6ea8f7d4a035f1836333b07e118bf0c89403e48fcd10ddd49c12102c8641b1b44dd27e6fd22ea9b76db523e9dfcea939922b4456340d523a8fef8247cb8bb9a999cc599acdf4cbc25604077b16ce997ad0a2a7be032230e8dff...
```

The original server sends an encrypted sequenced packet after ACKing the same
class of 113-byte client packet. Source2 does not.

## First Divergence

The first meaningful divergence is the 113-byte client `OP_Packet`:

```text
0009 0000 0019 44 ...
```

This packet is:

```text
OP_Packet
  sequence 0
  embedded OP_AppCombined
    subpacket 1: RSA/RC4 key material
    subpacket 2: encrypted OP_LoginRequestMsg
```

The original source path is:

- `EQStream::ProcessPacket`, `OP_Packet`
- `EQStream::HandleEmbeddedPacket`
- embedded `OP_AppCombined`
- `EQStream::ProcessPacket`, `OP_AppCombined`
- first subpacket calls `processRSAKey`
- second subpacket calls `ProcessEmbeddedPacket`
- `ProcessEncryptedData` decrypts login request
- `Client::Process` handles `OP_LoginRequestMsg`

The current source2 path is:

- `StreamPipeline::handle_app_packet`
- `handle_encryption_key_packet`
- extracts the RC4 key from the end of the whole packet payload
- ACKs sequence 0
- does not process remaining embedded subpacket data as encrypted login request

## Phase Implications

- Phase 2 must implement embedded `OP_AppCombined` behavior inside sequenced
  `OP_Packet`.
- Phase 3 must extract the RC4 key from the correct combined subpacket and
  continue processing the encrypted login subpacket in the same datagram.
- Phase 3 must also match legacy encrypted outbound app packet preparation.

