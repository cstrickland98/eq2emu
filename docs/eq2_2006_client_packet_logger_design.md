# EQ2 2006 Client Packet Logger Design

This design uses the client deserializer as the first parser. It captures
cleartext packet bytes and records every stream read/write performed by message
serialize/deserialize methods. That gives useful packet data before every field
has a human-assigned name.

## Why This Works

The network layer is:

```text
incoming UDP-library payload
  -> optional RC4-style decrypt
  -> control byte
  -> optional zlib inflate
  -> VeMemoryStream
  -> packed type ID
  -> VeType factory
  -> message vtable +0x08 deserialize
  -> ClientNet listener dispatch

outgoing message object
  -> message vtable +0x04 serialize
  -> packed type ID + message body in VeMemoryStream
  -> RC4-style encrypt
  -> UdpConnection::Send
```

So the useful logger hooks are above Winsock and around `VeMemoryStream`, not at
`send` / `recv`.

## Core Hook Points

| Address | Name | Logger use |
| --- | --- | --- |
| `0x0043bac9` | inside `ClientNet_HandleReceivedPayload_Maybe` | `ESI` = clear body pointer, `EDI` = clear body length before the body is attached to a stream. |
| `0x008fc60f` | inside `VeType_DeserializeObjectFromStream_Maybe` | Immediately before message `deserialize(stream)` call. `EDI` = message object, `ESI` = `VeType` metadata, `[EBP-4]` = type ID, `[EBP+8]` = stream. |
| `0x0075ab32` | `VeMemoryStream_ReadRaw_Maybe` | Central raw field read. `ECX` = stream, stack args are destination, byte count, strict flag. |
| `0x00472240` | `VeStream_ReadStringLenN_Maybe` | Length-prefixed string read helper. `ECX` = stream, args are target string object and length-byte count. |
| `0x0075e670` | `VeStream_ReadPackedTypeId_Maybe` | Packed uint16 reader. First use in `VeType_DeserializeObjectFromStream_Maybe` is the message type ID; later uses can be field lengths or nested IDs. |
| `0x008fc5ac` | inside `VeType_SerializeObjectToStream_Maybe` | Immediately before message `serialize(stream)` call. `EDI` = message object, `ESI` = stream. |
| `0x0075ad7e` | `VeMemoryStream_WriteRaw_Maybe` | Central raw field write. `ECX` = stream, stack args are source and byte count. |
| `0x00472380` | `VeStream_WriteStringLenN_Maybe` | Length-prefixed string write helper. |
| `0x0075e625` | `VeStream_WritePackedTypeId_Maybe` | Packed uint16 writer. First use in `VeType_SerializeObjectToStream_Maybe` is the message type ID. |
| `0x0043ad9f` | inside `ClientNet_SendMsg_Maybe` | Outgoing clear body immediately before RC4-style encryption. `EAX` = buffer, `EDI` = length, `ESI` = ClientNet. |

## Frida Prototype

The repo now has a first-pass Frida logger at
`scripts/frida_eq2_2006_packet_logger.js`. It hooks the addresses above and
emits JSON lines for:

- clear incoming and outgoing packet bodies
- packed message type ID reads/writes
- concrete message serialize/deserialize entry
- every `VeMemoryStream` raw field read/write while inside a packet context
- length-prefixed string reads/writes

Run it against a local client process:

```powershell
frida -n EverQuest2.exe -l scripts/frida_eq2_2006_packet_logger.js
```

or launch through Frida:

```powershell
frida -f "E:\Games\Everquest II\EverQuest2.exe" -l scripts/frida_eq2_2006_packet_logger.js --no-pause
```

The script writes to the Frida console and attempts to append
`eq2_packet_log.jsonl` in the target process working directory.

By default, the script now starts in a conservative mode:

- `ENABLE_STREAM_PACKET_BODY_LOG = true`
- `ENABLE_DANGEROUS_INTERIOR_PACKET_BODY_HOOKS = false`
- `ENABLE_DANGEROUS_INTERIOR_MESSAGE_CALL_HOOKS = false`
- `ENABLE_FIELD_TRACE = true`
- `ENABLE_STRING_TRACE = false`

The stream packet body logger captures clear bodies from the safe
serialize/deserialize root hooks. Leave the two `ENABLE_DANGEROUS_*` switches
off unless you are debugging the hook addresses themselves. They are
instruction-level hooks inside larger functions and have been observed to
destabilize this client during login.

## Analyzing Captures

After a run, summarize the JSONL log:

```powershell
python scripts\analyze_eq2_packet_log.py "E:\Games\Everquest II\eq2_packet_log.jsonl"
```

If the log is in another directory, pass that path instead. Multiple logs can be
combined in one run:

```powershell
python scripts\analyze_eq2_packet_log.py .\captures\login.jsonl .\captures\zone.jsonl
```

Outputs:

- `artifacts/eq2_packet_log_summary.md`
- `artifacts/eq2_packet_log_summary.json`

Use the Markdown report to find stable candidate layouts by
`direction + type_id`. Use the JSON report when generating fixtures or
comparing captures programmatically.

For deeper body decoding, especially zlib payloads and known login packet
fields, run:

```powershell
python scripts\decode_eq2_packet_log.py "E:\Games\Everquest II\eq2_packet_log.jsonl"
```

The decoder also understands metadata extracted from the legacy EQ2Emu
PacketParser package. Regenerate that metadata after changing the selected
client build:

```powershell
python scripts\extract_packetparser_metadata.py `
  --packetparser-dir "E:\_EQ2\eq2emu-tools\PacketParser" `
  --client-version 546
```

Then pass the generated opcode and struct files explicitly if needed:

```powershell
python scripts\decode_eq2_packet_log.py "E:\Games\Everquest II\eq2_packet_log.jsonl" `
  --opcodes artifacts\packetparser_metadata\opcodes_v546.csv `
  --structs artifacts\packetparser_metadata\structs_by_opcode_v546.json
```

Outputs:

- `artifacts/eq2_packet_decoded.md`
- `artifacts/eq2_packet_decoded.json`
- `artifacts/eq2_packet_decoded_payloads\*.txt`
- `artifacts/eq2_packet_decoded_payloads\*.bin`

The decoder defaults to the latest appended Frida session because
`eq2_packet_log.jsonl` is append-only. Delete or move the log before a fresh
capture if you want one clean session. PacketParser struct support is currently
a best-effort interpreter for fixed primitives, EQ2 strings, colors, arrays, and
substruct references. It is most useful on world packets whose XML structs exist;
login-server packets still need Ghidra/Frida validation where XML definitions are
missing or leave trailing bytes. Packet logs can include login credentials and
client crash reports, so avoid sharing raw captures casually.

## Runtime Context

Maintain a thread-local packet context while inside a message serialize or
deserialize method:

```c
struct PacketTraceContext {
  enum { Incoming, Outgoing } direction;
  uint16_t type_id;
  void *message_object;
  void *stream;
  uint32_t serialize_or_deserialize;
  uint32_t vtable;
  uint32_t sequence;
};
```

Set it at:

- `0x008fc60f` for incoming deserialization.
- `0x008fc5ac` for outgoing serialization.

Clear it after the virtual call returns.

## Stream Structure

For the `VeMemoryStream` used by network packets:

```c
struct VeMemoryStream {
  void **vtable;        // +0x00
  StreamBuffer *buffer; // +0x04
  uint8_t error;        // +0x08, set on short reads
};

struct StreamBuffer {
  uint32_t owns_buffer; // +0x00
  uint8_t *data;        // +0x04
  uint32_t size;        // +0x08
  uint32_t capacity;    // +0x0c
  uint32_t position;    // +0x10
};
```

For raw field hooks, record `buffer->position` before the original call and
`buffer->position` after the call. The bytes read are in the destination buffer
after `VeMemoryStream_ReadRaw_Maybe` returns.

## Field Event Schema

Each raw read/write event should record:

```json
{
  "direction": "incoming",
  "type_id": 213,
  "message_object": "0x12345678",
  "function": "0x00922c4f",
  "stream_offset": 6,
  "object_offset": 12,
  "size": 4,
  "bytes": "01 00 00 00",
  "u8": 1,
  "u16le": 1,
  "u32le": 1,
  "f32le": 1.401298e-45
}
```

`object_offset` is `destination - message_object` for reads, or
`source - message_object` for writes, when the pointer is inside the message
object allocation. If it is outside the object, log it as a temporary buffer.

## Clear Packet Record Schema

Also log a packet-level record:

```json
{
  "direction": "incoming",
  "type_id": 213,
  "inferred_name": "VeLsClientBaselogReplyMsg",
  "clear_length": 123,
  "clear_body_hex": "ff d5 00 ...",
  "deserialize": "0x00922c4f",
  "vtable": "0x00ce06d8",
  "events": []
}
```

For outgoing packets, include:

```json
{
  "reliable": true,
  "udp_channel": 4
}
```

`ClientNet_SendMsg_Maybe` uses channel `4` for reliable sends and `0` for
unreliable sends.

## Static Registry

Use the extractor script to regenerate the type map:

```powershell
python scripts/extract_eq2_client_packet_registry.py `
  --exe "E:\Games\Everquest II\EverQuest2.exe" `
  --csv artifacts/eq2_client_packet_registry.csv `
  --json artifacts/eq2_client_packet_registry.json
```

The current extraction finds:

- 407 registered message types.
- Type ID range `0..410`.
- 25 names directly inferred from debug/assertion strings.
- The client-derived log reply family uses the shared compressed blob serializer
  for types `213..217`: baselog, crashlog.txt, eq2_crash.log, alertlog.txt,
  and verifylog.txt respectively. This differs from the legacy PacketParser
  v546 labels at the 216/217 boundary.

The generated table provides:

- `type_id`
- inferred name when available
- `VeType` metadata address
- factory function
- vtable
- serialize function
- deserialize function
- evidence string

## Parser Strategy

1. Capture packet-level clear bytes at `0x0043bac9` and `0x0043ad9f`.
2. Decode the first packed type ID from the clear body.
3. Look up the type ID in `artifacts/eq2_client_packet_registry.json`.
4. While the client deserializes/serializes the packet, record every raw
   stream read/write as a field event.
5. Use `object_offset`, stream offset, and function address to build stable
   field definitions.
6. Once a field layout is confirmed, move it into an eq2emu-side parser for
   that type ID.

This produces useful logs immediately, even when field names are not known:

```text
incoming type=213 VeLsClientBaselogReplyMsg deserialize=0x00922c4f len=...
  +0x0000 obj+0x04 read u32 0x00000120
  +0x0004 obj+0x08 read u32 0x00000000
  +0x0008 temp    read bytes[288] ...
```

## Important Caveat

This does not automatically produce semantic names like `character_id` or
`zone_id`. It produces the complete decrypted/decompressed packet bytes and the
client's own field access trace. Human naming still comes from matching object
offsets and handlers to gameplay behavior, strings, and eq2emu structures.
