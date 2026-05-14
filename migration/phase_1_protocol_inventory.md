# Phase 1 Protocol Inventory

Status: complete.

## Known Areas To Inventory

| Area | Current files to inspect |
| --- | --- |
| Opcode names and lookup | `source/common/emu_opcodes.*`, `source/common/opcodemgr.*` |
| Packet structures | `source/common/PacketStruct.*`, `source/common/packet_functions.*` |
| Packet headers | `source/LoginServer/PacketHeaders.*`, world/client packet code |
| Stream handling | `source/common/EQStream.*`, `source/common/EQStreamFactory.*` |
| TCP framing | `source/common/TCPConnection.*` |
| Login packet handling | `source/LoginServer/client.*` |
| World/client packet handling | `source/WorldServer/client.*`, `source/WorldServer/ClientPacketFunctions.cpp` |
| Crypto/CRC | `source/common/Crypto.*`, `source/common/RC4.*`, `source/common/CRC16.*` |

## Current Protocol Boundaries

| Responsibility | Current owner | Source2 owner |
| --- | --- | --- |
| UDP stream/session handling | `EQStream`, `EQStreamFactory` | `eq2::net` plus protocol framing helpers |
| Stream session request/response structures | `EQStream.h` | `eq2::protocol` |
| Packet opcode maps | Global `EQOpcodeManager` loaded by login/world from DB | `eq2::protocol` |
| Struct-driven packet layouts | `ConfigReader`, `PacketStruct`, XML struct files | `eq2::protocol` |
| Login packet handlers | `source/LoginServer/client.cpp` | `eq2::login` |
| World packet handlers | `source/WorldServer/client.cpp` | `eq2::world` or `eq2::zone` depending on ownership |
| Login-world server packets | `ServerPacket`, `LWorld`, `LoginServer` | protocol model shared by login/world interserver boundary |
| Compression/encryption | `EQStream`, `EQPacket`, zlib, `Crypto`, `RC4` | protocol framing with net-owned byte transport |

## Flows To Trace

### Login Flow

1. Client connects to login server.
2. Login packet is received.
3. Packet is decoded and version/account fields are interpreted.
4. Login DB lookup is performed.
5. Login response packet is emitted.

Current details:

- `OP_LoginRequestMsg` is parsed in `source/LoginServer/client.cpp`.
- Client version is first parsed using `LS_LoginRequest` version `1`; newer packet shape is retried with version `1208`.
- Opcode manager lookup uses `GetOpcodeVersion(version)` and global `EQOpcodeManager`.
- World and character lists are serialized through `PacketStruct` definitions loaded from XML.

### World Registration Flow

1. World server connects to login server.
2. World server authenticates/registers.
3. Login server records world availability.
4. Login server returns world list or handoff info to client.

Current details:

- World sends `ServerOP_LSInfo` and `ServerOP_LSStatus` from `source/WorldServer/LoginServer.cpp`.
- Login receives and validates these in `source/LoginServer/LWorld.cpp`.
- Login refuses non-`ServerOP_LSInfo` packets until the world connection is authenticated.

### World Entry Flow

1. Client connects to world server.
2. World server validates login/handoff data.
3. Character list is sent.
4. Character selection is handled.
5. Zone entry packets begin.

Current details:

- Client sends `OP_LoginByNumRequestMsg`.
- World parses `LoginByNumRequest`, with old/new version fallback similar to login.
- World validates `ZoneAuthRequest` by account ID and access key.
- Zone sets spawn packet substructs using `ConfigReader` names like `Substruct_SpawnPositionStruct`, `WS_SpawnStruct_Header`, and `WS_SpawnStruct_Footer`.

## Source2 Implications

- `eq2::protocol` should own packet headers, opcode lookup, and versioned serialization.
- `eq2::net` should own byte transport and session lifecycle.
- Packet handlers should live in `eq2::login`, `eq2::world`, or `eq2::zone`, not in protocol/net.
- Golden packet tests should be created before replacing packet code.

## Characterization Targets For Later Review

These are not phase 2 work yet, but they are the key behavior points found in phase 1:

- Login request version fallback: version `1` then `1208`.
- World login request version fallback: version `1` then `1208`.
- Opcode DB load from `opcodes` by version range.
- `ServerOP_LSInfo` authentication gate for world connections.
- `ServerOP_UsertoWorldReq` and `ServerOP_UsertoWorldResp` routing through login.
- `ZoneAuthRequest` access key handoff.
- Packet struct loading from XML files during startup.

## Remaining Review Questions

- Identify exact packet header layouts used by login and world.
- Identify version-specific opcode maps.
- Identify compression/encryption order.
- Identify CRC responsibilities.
- Identify where packet structs are loaded from XML or other data files.

These questions should be resolved by characterization tests and source2 protocol design, not by expanding phase 1 indefinitely.

## Phase 1 Protocol Exit Result

Protocol inventory is sufficient for phase 2 planning. The migration must separate transport, framing, opcode/packet definitions, and application handlers.
