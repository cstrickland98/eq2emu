# EQ2 2006 Client Packet Reversing Notes

Target in Ghidra: `EverQuest2.exe`, image base `0x00400000`, `x86:LE:32:default`.

## Transport Anchors

Winsock imports are present by ordinal. The direct `send` / `recv` wrappers are generic byte-queue socket helpers:

| Address | Ghidra name | Notes |
| --- | --- | --- |
| `0x007610f2` | `Network_SendRaw_Maybe` | Calls `send`, retries until timeout / full length. |
| `0x007616d6` | `Network_SendOrQueueBytes_Maybe` | Sends immediately or appends to internal send queue. |
| `0x0076159e` | `Network_FlushQueuedSend_Maybe` | Flushes queued chunks through `Network_SendRaw_Maybe`. |
| `0x0076119f` | `Network_ReadBuffered_Maybe` | Reads from internal recv queue, pumps `recv` as needed. |
| `0x00761347` | `Network_PumpRecvToQueue_Maybe` | Calls `recv` into 0x400-byte chunks. |
| `0x007614b4` | `Network_PollConnectionAlive_Maybe` | Uses `recv(..., MSG_PEEK)` to detect closed sockets. |
| `0x00766add` | `ByteQueue_Append_Maybe` | Appends bytes into 0x400-byte queue chunks. |
| `0x007668b6` | `ByteQueue_ReadConsume_Maybe` | Copies and consumes queued bytes. |
| `0x00766550` | `ByteQueue_GetSize_Maybe` | Returns queued byte count. |
| `0x0076669c` | `ByteQueue_PeekChunk_Maybe` | Returns first chunk pointer and length. |

The game protocol path is more useful than the raw Winsock wrappers. It goes through `ClientNet.cpp` and SOE `UdpLibrary`:

| Address | Ghidra name | Notes |
| --- | --- | --- |
| `0x0043b9e0` | `ClientNet_HandleReceivedPayload_Maybe` | Main receive-side packet entry. |
| `0x0043ad30` | `ClientNet_SendMsg_Maybe` | Main send-side message serializer/encrypt/send path. |
| `0x00844c70` | `UdpConnection_Send_Maybe` | Sends reliable/unreliable UDP-library payload. |
| `0x0075d4ad` | `RC4_ApplyInPlace_Maybe` | RC4-style PRGA mutating a buffer in place. |
| `0x00857f8d` | `Zlib_Inflate_Maybe` | zlib inflate implementation. |
| `0x0043b380` | `ClientNet_InitCryptoAndZlib_Maybe` | Initializes zlib and RC4-like key state after handshake. |

Lower-level UDP-library anchors:

| Address | Ghidra name | Notes |
| --- | --- | --- |
| `0x00841ee0` | `UdpManager_RouteIncomingDatagram_Maybe` | Routes raw UDP datagrams by endpoint/session, creates sessions from `0x00 0x01`, and dispatches known connections to receive processing. |
| `0x00845e40` | `UdpConnection_ProcessReceivedDatagram_Maybe` | Verifies configured CRC/check bytes, applies inverse transform layers, and passes decoded bytes to the SOE frame parser. |
| `0x00844e40` | `UdpConnection_ProcessSoeUdpFrame_Maybe` | Main SOE UDP frame parser. |
| `0x00843260` | `UdpConnection_DeliverApplicationPayload_Maybe` | Delivers decoded application payload bytes after SOE UDP framing is removed. |
| `0x00841770` | `UdpConnection_UpcallApplicationPayload_Maybe` | Calls the higher-level application callback used by `ClientNet`. |
| `0x00844810` | `UdpConnection_SendByChannel_Internal_Maybe` | Dispatches outgoing payloads by logical channel `0..7`. |
| `0x008444b0` | `UdpConnection_BufferOrSendMulti_Maybe` | Buffers small outgoing subpackets into a `0x00 0x03` multi-packet frame or sends large payloads directly. |
| `0x00844360` | `UdpConnection_FlushMultiPacket_Maybe` | Flushes the pending multi-packet buffer; strips the multi header when only one subpacket is queued. |
| `0x00844150` | `UdpConnection_SendWireDatagram_Maybe` | Applies outgoing transform/check layers before raw UDP send. |
| `0x008466c0` | `UdpConnection_Crc32Seeded_Maybe` | Seeded CRC32-style check function using table `DAT_00e793b0`. |
| `0x00849090` | `UdpReliableStream_SendPayload_Maybe` | Reliable stream send entry for logical channels `4..7`. |
| `0x00848460` | `UdpReliableStream_PumpSendWindow_Maybe` | Emits reliable data/fragment frames as `0x09 + channel` or `0x0d + channel`. |
| `0x00848bc0` | `UdpReliableStream_HandleDataOrFragment_Maybe` | Receives reliable data/fragment frames `0x09..0x10` and returns ack variants. |
| `0x008467e0` / `0x00846840` | `UdpSoeVarLen_Write_Maybe` / `UdpSoeVarLen_Read_Maybe` | Variable-length size codec used by `0x00 0x19` app-combined frames. |

## Packet Flow

Incoming:

1. `UdpManager_RouteIncomingDatagram_Maybe` routes the raw UDP datagram. A new session can be created from a `0x00 0x01` request; known endpoints are dispatched to `UdpConnection_ProcessReceivedDatagram_Maybe`.
2. `UdpConnection_ProcessReceivedDatagram_Maybe` verifies any configured CRC/check trailer with `UdpConnection_Crc32Seeded_Maybe`, applies inverse transform layers, and calls `UdpConnection_ProcessSoeUdpFrame_Maybe`.
3. `UdpConnection_ProcessSoeUdpFrame_Maybe` strips SOE UDP framing. Application payload bytes are delivered through `UdpConnection_DeliverApplicationPayload_Maybe` and `UdpConnection_UpcallApplicationPayload_Maybe`.
4. `ClientNet_HandleReceivedPayload_Maybe` decrypts the UDP-library payload with `RC4_ApplyInPlace_Maybe` when key state is active. The receive RC4 state is at `ClientNet + 0x162`.
5. It consumes a leading packet control byte:
   - `0x01`: zlib-compressed body. Inflates into `DAT_00e9b010`, max `500000` bytes. Assertion string: `u_readlen < PACKET_DECOMPRESSED_MAXSIZE`.
   - `0x02`: initial crypto/key handshake when no key is active.
   - other values: normal uncompressed body. This is likely `0x00` for ordinary message payloads.
6. The remaining body is attached to a `VeMemoryStream`.
7. `VeType_DeserializeObjectFromStream_Maybe` reads the packed message type ID, looks it up with `VeType_FindById_Maybe`, instantiates the registered class, and calls the class vtable `+0x08` deserialize method.
8. Registered ClientNet listeners are called via listener vtable `+0x14` with the deserialized message object.

Outgoing:

1. `ClientNet_SendMsg_Maybe` creates a memory stream.
2. `VeType_SerializeObjectToStream_Maybe` writes the packed type ID, then calls the message vtable `+0x04` serialize method.
3. The serialized bytes are encrypted with `RC4_ApplyInPlace_Maybe`. The send RC4 state is at `ClientNet + 0x68`.
4. The encrypted payload is sent through `UdpConnection_Send_Maybe`, using channel `4` for reliable sends and `0` for unreliable sends.
5. `UdpConnection_Send_Maybe` preserves an application payload that starts with `0x00` as a one-byte prefix plus a second data buffer, then calls `UdpConnection_SendByChannel_Internal_Maybe`.
6. `UdpConnection_SendWireDatagram_Maybe` applies configured outgoing transform layers and appends `1..4` seeded CRC/check bytes before raw UDP send when the connection requests them.

The `VeType` message ID prefix is packed as `u8` for IDs below `0xff`; IDs `0xff` and above use marker `0xff` followed by little-endian `u16`. This matters for recovered client types above `255`.

The current static registry extraction (`artifacts/eq2_client_packet_registry.*`)
finds 407 root message classes, with the highest recovered root type id being
`410`. No root client message type `999` is registered in this DoF executable;
PacketParser v546 rows such as `999,OP_MapRequest` should therefore not be
treated as recovered client `VeType` ids without separate send-site evidence.

Source2 login and world client-session paths should therefore use packed
application opcode width for the 2006 client. The older fixed two-byte app
header remains useful only for isolated harnesses that intentionally emit that
legacy source2 format.

Outgoing channel behavior in `UdpConnection_SendByChannel_Internal_Maybe`:

| Channel | Behavior |
| --- | --- |
| `0` | Buffer/send through `UdpConnection_BufferOrSendMulti_Maybe` without the sequenced-data flag. |
| `1` | Copy payload plus optional extra buffer and send directly through `UdpConnection_SendWireDatagram_Maybe`. |
| `2` | Prepend `0x00 0x1b` plus a big-endian `u16` sequence, then buffer/send through the multi-packet path with the sequenced-data flag. |
| `3` | Prepend `0x00 0x1b` plus a big-endian `u16` sequence and send directly through the wire datagram path. |
| `4`-`7` | Route through per-channel reliable stream objects, lazily allocating them if needed. Reliable channel index `0..3` is encoded by adding the index to opcode bases `0x09`, `0x0d`, `0x11`, and `0x15`. |

Channels `0..3` can be promoted to channel `4` when the current send window cannot fit the payload. The multi-packet path starts frames with `0x00 0x03`, then stores one-byte subpacket lengths followed by subpacket bytes.

Reliable stream payloads can be batched into a `0x00 0x19` app-combined frame. Its subpacket length codec is `u8 length` for values `< 0xfe`, `0xff` plus big-endian `u16` for values `< 0xffff`, or `0xff 0xff 0xff` plus big-endian `u32` for larger lengths. If only one subpacket was batched, the client strips the `0x00 0x19` frame and sends the subpacket directly.

SOE UDP frame opcodes handled by `UdpConnection_ProcessSoeUdpFrame_Maybe`:

| Frame | Behavior |
| --- | --- |
| nonzero first byte, or length `< 2` | Deliver directly as application payload. |
| `0x00 0x00` | Strip the leading `0x00` and deliver the remainder as application payload. |
| `0x00 0x01` | Session request. Reads two big-endian dwords, validates/sets session state, and can reply with `0x00 0x02`. |
| `0x00 0x02` | Session response. Reads session/config values, CRC/compression bytes, MTU/window values, then opens the connection when the id matches. |
| `0x00 0x03` | Multi-packet frame. Iterates one-byte length-prefixed subpackets and recursively parses each subpacket as another SOE UDP frame. |
| `0x00 0x05` | Disconnect-style frame; validates the session id and closes/schedules close. |
| `0x00 0x07` | Stats/ping request; stores remote stats and replies with `0x00 0x08`. |
| `0x00 0x08` | Stats/ping reply; computes round-trip timing and updates min/avg/max stats. |
| `0x00 0x09`-`0x00 0x10` | Reliable channel data/fragment group routed to per-channel stream objects. `0x09..0x0c` are data, `0x0d..0x10` are fragment variants. |
| `0x00 0x11`-`0x00 0x14` | Reliable out-of-order ack group for reliable channel indexes `0..3`. |
| `0x00 0x15`-`0x00 0x18` | Reliable ack group for reliable channel indexes `0..3`. |
| `0x00 0x19` | Combined/nested packet frame. Reads the SOE variable-length sizes above with `UdpSoeVarLen_Read_Maybe` and recursively parses each nested frame. |
| `0x00 0x1a` | Sequenced application payload using connection sequence state at `this + 0x1c8`; strips the four-byte wrapper before delivery. |
| `0x00 0x1b` | Sequenced application payload using connection sequence state at `this + 0x1ca`; strips the four-byte wrapper before delivery. |
| `0x00 0x1d` | Disconnect/ping-style request; can answer with `0x00 0x1e` and two packed dwords when the option is enabled. |

Session response payload after `0x00 0x02` is fixed-order big endian: `u32 session`, `u32 crc_seed`, `u8 crc_bytes`, `u8 transform_a`, `u8 transform_b`, `u32 max_length`, optional `u32` trailing value. The client library's own session-response writer emits trailing value `2`.

## Login-Scene Packet Handlers

These handlers were found from the login/world-list diagnostic strings and
cross-references in the current Ghidra project. There are two related receive
paths: the autologin/login-scene dispatcher at `0x004d5f70`, and a
character-select dispatcher at `0x004ea750`.

| Address | Ghidra name | Packet role | Notes |
| --- | --- | --- | --- |
| `0x004d5f70` | `LoginScene_OnReceiveMsg_Maybe` | Top-level login/autologin dispatch | Calls the message vtable `+0x0c` type-id accessor, then switches on the returned `uint16`. Handles login reply type `4` inline, world list type `8`, all-characters reply type `10`, and play-character reply type `0x13`. On accepted login it also uploads/deletes local crash, alert, and verify logs through the compressed-log reply message rows. |
| `0x004ea750` | `CharacterSelect_OnReceiveMsg_Maybe` provisional | Character-select message dispatch | Calls the same vtable type-id accessor and handles world-status type `6`, world list type `8`, all-characters reply type `10`, create-character reply type `12`, delete-character reply type `17`, play-character reply type `19`, and keymap rows `154`/`155`. |
| `0x004d4b10` | `LoginScene_HandleWorldListMsg_Maybe` | World list response | Treats the message payload as a vector of `0x44`-byte world records between object offsets `+0x04` and `+0x08`; selects the autologin world and sends an `OP_AllCharactersDescRequestMsg`-style request through `ClientNet_SendMsg_Maybe`. |
| `0x004d5020` | `LoginScene_HandleAllCharactersDescReply_Maybe` | Character-list response | Treats the message payload as a vector of `0x1cc`-byte character records between object offsets `+0x04` and `+0x08`; matches selected world id/name and sends an `OP_PlayCharacterRequestMsg`-style request. |
| `0x004d5460` | `LoginScene_HandlePlayCharacterReply_Maybe` | Play-character reply | Success code `1` connects onward with address string at object `+0x08`, port at `+0x1c`, account number at `+0x20`, and passcode at `+0x24`. |
| `0x004e9ef0` | character-select world-list handler | World list response | Iterates the `0x44`-byte world records, inserts/updates cached world entries, refreshes the world list UI, and updates selection state. |
| `0x004ea000` | character-select character-list handler | Character-list response | Rebuilds the character-select UI from the `0x1cc`-byte character records, applies the account/character count trailer, creates character display objects, and selects the cached/first matching character. |
| `0x004db7a0` | create-character reply handler | Create-character reply | Switches on response byte at object `+0x08`; response `1` succeeds and refreshes character-list state, while responses `2..14` map to concrete UI error text. |
| `0x004db650` | delete-character reply handler | Delete-character reply | Switches on response byte at object `+0x04`; response `1` succeeds, response `3` reports character-not-found, and other values report generic delete failure. |
| `0x004e2f70` | character-select play reply handler | Play-character reply | Success code `1` caches address string at object `+0x08`, port `+0x1c`, account number `+0x20`, and passcode `+0x24`; failure codes `3`, `4`, `5`, `6`, `7`, `10`, and `11` map to concrete UI error text. |
| `0x004de940` | `LoginScene_HandleWorldStatusChangeMsg_Maybe` | World status update | Message object offsets: `+0x04` world id, `+0x08..+0x0b` four one-byte status/load/flag fields. Called from the character-select dispatcher; looks up the cached world record and copies those bytes to cached offsets `+0x40..+0x43`. |
| `0x0046c330` | `LoginReply_StateGate_Maybe` | Login accepted/rejected state gate | Checks object offset `+0x04` as the login reply code; zero means accepted and advances state `this+0x44 = 4`. |

## Game-Scene Packet Handlers

`GameScene_OnReceiveMsg_Maybe` starts at `0x004b2740`. Ghidra has not
created a function body there, but the listener vtable slot `+0x14` points to
that address. The dispatcher reads the message vtable `+0x0c` type-id
accessor, subtracts `4`, bounds-checks through type `410`, and uses a byte
index table at `0x004b353c` plus jump table `0x004b3338`. The default branch at
`0x004b3300` logs `Unhandled network message (%s)` from `GameScene.cpp` line
`0x1761`, so rows routed there are known to be unhandled by GameScene rather
than just not yet decompiled.

Handler-backed packet names recovered from this dispatcher:

| Type | Handler | Packet role | Notes |
| --- | --- | --- | --- |
| `4` | `0x0046c330` | `OP_LoginReplyMsg` | Logs the login reply; reply code `0` advances GameScene state to `4` while nonzero replies log rejection. |
| `6` | `0x004b332d` | `OP_WorldStatusChangeMsg` inline no-op | Nondefault GameScene case that returns success without reading the body. The serializer/deserializer still define the `u32` plus four-byte status body. |
| `33` | `0x004b27b3` | `OP_DoneLoadingUIResourcesMsg` inline case | Empty loading-state row; sets global loaded-state flag `DAT_00f17428` and returns. This is the server echo/ack the loading state machine waits for after sending type `3`, not the `OP_GameWorldTimeMsg` handler. |
| `29` | `0x004ad910` | `OP_ZoneInfoMsg` | Logs `Zone info msg`, copies the zone/region/environment strings and slideshow/location data into GameScene state, updates tutorial and environment flags, refreshes zone UI/map state, and advances GameScene state to `5`. |
| `31` | `0x00472d00` | `OP_DoneSendingInitialEntitiesMsg` | Empty payload; logs initial-entity send completion, calls the loading/UI transition helper, and sets GameScene state `0x13`. |
| `35` | `0x004b0090` | `OP_SetRemoteCmdsMsg` | Logs `Recieved remote command table (%d commands)`, installs command callback records from the primary command list, registers aliases from the secondary list, and refreshes the command table. |
| `37` | `0x0046c420` | `OP_GameWorldTimeMsg` | Shares the generic scene-ready handler; when the 3D scene object is active it forwards the body at message `+0x04` to the scene-time update path. |
| `43` | `0x0048e700` | `OP_CampStartedMsg` | Incoming camp-start shape: passes the seconds byte and camp-desktop flag at object `+0x04/+0x05` into the camp timer/state helper. |
| `44` | `0x00484f80` | `OP_CampAbortedMsg` | Empty payload. If the client is preparing to camp, clears the camp-prep flag and displays "You abandon your preparations to make camp." |
| `46` | `0x00490090` | `OP_WhoQueryReplyMsg` | Formats `/who` output, including no-result and capped-result states, and prints per-player level/class/race/flags/zone text from the reply records. |
| `50` | `0x004ae310` | `OP_ClientCmdMsg` | Attaches the raw blob as a `VeMemoryStream`, deserializes nested `VeType` client-command objects, and dispatches subtypes `0x19b..0x1ef` through a second-level command switch. |
| `55` | `0x004a1790` | `OP_UpdateCharacterSheetMsg` | Decodes the packed `0x1321`-byte character sheet at message `+0x04` against the cached world-data sheet, copies the decoded sheet into both world-data and the global character-sheet mirror, then marks character/UI panels dirty when key stats or zone/bind state change. |
| `56` | `0x004a6030` | `OP_UpdateSpellBookMsg` | Logs `m_processUpdateSpellBookMsg()`, clears the main spellbook tree, decodes packed `0x1b`-byte spell records into world-data `+0x1321..+0x1325`, rebuilds the lookup tree at `+0x3e14`, and refreshes knowledge/spellbook UI state. |
| `58` | `0x004a1dd0` | `OP_UpdateInventoryMsg` | Uses the trailing equip/inventory flag to choose the cached `0x6e`-stride item vector at world-data `+0x1330..+0x1334` or `+0x133c..+0x1340`, decodes the packed item delta, refreshes inventory/equipment UI, and updates bag/equipment warning state. |
| `59` | `0x004a2420` | `OP_AfterInvSpellUpdate` | Same packed-delta envelope and `0x1b` record stride as `OP_UpdateSpellBookMsg`, but updates the secondary spellbook-adjacent state at world-data `+0x1348..+0x134c`. The EQ2Emu source XML carries this name in a guessed `ClientVersion="1"` struct; the handler evidence confirms the row shape and role. |
| `60` | `0x004a19a0` | `OP_UpdateRecipeBookMsg` | Decodes the `0x0c`-stride recipe array into world-data `+0x13a4..+0x13a8`, builds a request for missing recipe detail records when needed, or refreshes the recipe-book UI when all referenced recipes are known. |
| `62` | `0x0048feb0` | `OP_RecipeDetailsMsg` | Iterates `0x2d6`-byte recipe-detail records from message `+0x04..+0x08`, inserts/updates each recipe in the client recipe cache through `0x0048a3f0`, then refreshes the recipe book. |
| `63` | `0x004a1c20` | `OP_UpdateSkillBookMsg` | Decodes packed `0x15`-byte skill records into world-data `+0x13b0..+0x13b4`, rebuilds the skill lookup/state, refreshes skill UI subsystems, and marks the character/knowledge panels dirty. Type `64` uses the same wrapper shape but is not routed through this GameScene handler. |
| `65` | `0x00473c40` | `OP_UpdateOpportunityMsg` | Passes the embedded Heroic Opportunity object at message `+0x04` to the opportunity UI/state paths and marks the character/knowledge panel dirty. |
| `67` | `0x00491d40` | `OP_ChangeZoneMsg` | Stores pending zone address/key data at GameScene `+0x26c8..+0x26e8`, sets the zone-change flag, sends the ready/ack message, and destroys the request object. |
| `69` | `0x004b0240` | `OP_TeleportWithinZoneMsg` | Logs `Teleport within zone msg` and passes the three-float coordinate vector at object `+0x04` to the teleport helper. |
| `70` | `0x00473d30` | `OP_TeleportWithinZoneNoReloadMsg` | Logs `Teleport within zone no reload msg`, applies coordinates plus heading without a full zone reload, and refreshes camera/scene state. |
| `89` | `0x0046c510` | `OP_ClearDataMsg` | Requires data-type byte `1`, clears a 100-entry cached table, resets GameScene status state, and logs an assertion if another data type is received. |
| `90` | `0x00491e10` | `OP_ESZoneInstanceStatusMsg` | Stores the trailing `u16` status value and forwards the status plus two string fields to the instance-status UI/state helper. |
| `94` | `0x00479d10` | `OP_ZonesStatusMsg` | Iterates the nested zone-server/instance status lists, prints `EverQuest II Zone Servers and Instances`, and totals server/instance/client/entity/actor counts. |
| `110` | `0x004b1d50` | `OP_GuildUpdateMsg` | Processes the shared guild update payload, including guild-level-up display/sound handling, guild status and rank/permission refreshes, membership UI updates, and rank permission matrix propagation. |
| `123` | `0x00473e50` | `OP_PlayerHouseAccessUpdateMsg` | Resolves the `PlayerHouse` UI and refreshes access/upkeep/visitor state through `0x00695640`, including access vectors, permissions buttons, and house item action state. |
| `124` | `0x00473e70` | `OP_PlayerHouseDisplayStatusMsg` | Resolves the `PurchaseHouse` UI and updates sale/status display fields through `0x0069b550`, including cost/upkeep/status text and buy-button enabled state. |
| `125` | `0x00473e90` | `OP_PlayerHouseCloseUIMsg` | Resolves the `PlayerHouse` UI and handles close/result status through `0x00693a20`; this is the row that updates/removes individual access-list rows in the house UI. |
| `126` | `0x00473eb0` | buy-house status/response row | Routes the one-byte status/response to both `PlayerHouse` and `PurchaseHouse` UI handlers when those windows exist; client strings cover no permission, upkeep unpaid, not enough money/status, inventory full, and successful item collection cases. |
| `127` | `0x00695c30` | `OP_BuyPlayerHouseTintMsg` provisional | Empty payload row whose GameScene case only resolves the `PlayerHouse` UI. The opcode name follows source order, but the handler does not expose additional field semantics. |
| `128` | send site `0x0069b9cc` | `OP_BuyPlayerHouseMsg` | PurchaseHouse Buy button path sends vtable `0x00d059e8` (registry type `128`) with the selected `house_id` as the single `u32` body. |
| `138` | `0x0046c590` | `OP_MoveableObjectPlacementCriteria` | Copies the three `u32` body values into GameScene placement state at `+0x38c..+0x394`. |
| `139` | `0x0046c5c0` | `OP_EnterMoveObjectModeMsg` | Passes spawn id, placement mode, model/type fields, two float/raw fields, and counted id-list tail into the move-object mode helper `0x00535120`. |
| `140` | send site `0x00534870` | `OP_PositionMoveableObject` | The final `place_item` path builds this stack message and calls `ClientNet_SendMsg_Maybe`: `u32 spawn_id`, three coordinate floats, derived heading/yaw float, derived pitch float, and trailing `u32`. |
| `141` | send site `0x005349d0` | `OP_CancelMoveObjectModeMsg` | Restores cached moved-object state at GameScene `+0x398`, sends vtable `0x00ce70dc` with the single `u32 spawn_id` when called with the acknowledgement flag, then frees the cached state. |
| `142` | `0x004a6530` | `OP_HouseCustomizationScreenMsg` | Iterates the customization id/string tree from the message and applies changed entries against the GameScene customization tree at `+0xec` through helper `0x004a2610`. |
| `143` | `0x004ab450` | `OP_CustomizationPurchaseRequestMsg` | Applies the packed customization selection list at message `+0x04` through `0x004a7a10` and stores the trailing flag at GameScene `+0xf8`. |
| `144` | `0x00473ef0` | `OP_CustomizationSetRequestMsg` | Resolves the `InteriorCustomization` UI and rebuilds its shader/color option lists through `0x00603a10` from the complex variable-length entry list. |
| `147` | `0x00473f10` | `OP_CustomizationReplyMsg` | Routes the `u32,u32,u8` body to `Eq2GuiInteriorCustomizationWindow::update`; response codes include not-enough-money/status and success paths. |
| `151` | `0x0046c390` | `OP_UISettingsResponseMsg` | Attaches the `u16`-sized settings blob and trailing `u16` to a memory stream, then applies it through the UI/settings loader. |
| `154` | `0x004fd080` | `OP_KeymapNoneMsg` | Reached through the generic keymap/UI handler for type `0x9a`; resets/defaults keymap state without reading payload bytes. |
| `155` | `0x004fd080` | `OP_KeymapDataMsg` | Reached through the generic keymap/UI handler for type `0x9b`; attaches the raw keymap buffer and parses it with `0x004fb0d0`. |
| `160` | `0x004aa7d0` | `OP_EntityVerbsReplyMsg` | Resolves the `RadialMenu` UI, validates the target spawn id, rebuilds verb entries from the `0x44`-byte records, and emits `apply_verb` commands for immediate verbs. |
| `162` | `0x004adcb0` | `OP_ChatRelationshipUpdateMsg` | Switches on the relationship update type byte, updates friend/ignore-style caches from the `0x28`-byte records, may emit matched-name commands, and refreshes social UI/state. |
| `164` | `0x00472d30` | `OP_StoppedLootingMsg` | Resolves the loot-related UI for the spawn id at message `+0x04` and clears/hides the active loot presentation through `0x006e7230`. |
| `165` | `0x00472fe0` | `OP_SitMsg` | Empty payload; toggles the local player into seated state and updates sit/stand action availability. |
| `166` | `0x00473040` | `OP_StandMsg` | Empty payload; toggles the local player out of seated state and updates sit/stand action availability. |
| `169` | `0x004c5fb0` | `OP_ClearForTakeOffMsg` | Empty payload; resets the takeoff/flight state timer field at `+0x16c` to `-1.0`. |
| `170` | `0x004c5f90` | `OP_ReadyForTakeOffMsg` | Empty payload; arms the takeoff/flight state timer field at `+0x16c` when it is negative. |
| `176` | `0x004fd080` | `OP_DefaultGroupOptionsMsg` | Reached through the generic keymap/UI handler for type `0xb0`; applies the six group-default option bytes when the target UI/state object is enabled. |
| `178` | `0x00472d60` | `OP_DisplayGroupOptionsScreenMsg` | Resolves the `GroupOptions` UI and updates the visible loot/group option controls from the seven option bytes. |
| `179` | `0x00472d80` | `OP_DisplayInnVisitScreenMsg` | Resolves the `VisitInnRoom` UI, loads the counted house/owner list from message `+0x04`, and refreshes the visit-inn-room screen. |
| `187` | `0x00473080` | `OP_PerformPlayerKnockbackMsg` | Applies the five-float knockback body to the local player by deriving a direction vector from the target coordinates plus vertical/horizontal force values. |
| `188` | `0x0046c460` | `OP_PerformCameraShakeMsg` | Routes the single float intensity to the camera shake helper. |
| `189` | `0x004ae180` | `OP_PopulateSkillMapsMsg` | Clears and rebuilds the client skill-map caches from the counted skill id/name records. |
| `192` | `0x00473280` | `OP_ShowCreateFromRecipeUIMsg` | Resolves the `TradeSkills` UI and opens/populates the create-from-recipe view from the complex recipe/category payload. |
| `193` | `0x004732a0` | `OP_CancelCreateFromRecipeMsg` | Empty payload; resolves `TradeSkills`, hides crafting process/result panels, and restores the main recipe selection view. |
| `195` | `0x00473400` | `OP_StopItemCreationMsg` | Empty payload; resolves `TradeSkills`, reenables stop/close/action controls, and restores close-on-top behavior after crafting stops. |
| `196` | `0x004732c0` | `OP_ShowItemCreationProcessUIMsg` | Resolves `TradeSkills`, disables close-on-top, and opens the five-slot item creation process view from the item-detail slot payload. |
| `197` | `0x004732e0` | `OP_UpdateItemCreationProcessUIMsg` | Applies the process update to `TradeSkills`, then applies positive/negative progress and durability deltas at message `+0x30` and `+0x2c` to the active combine target when the target object exists. |
| `198` | `0x00473420` | `OP_DisplayTSEventReactionMsg` | Single-byte tradeskill event/reaction row; resolves `TradeSkills` and routes through the reaction display helper. |
| `199` | `0x00473450` | `OP_ShowRecipeBookMsg` | Resolves `TradeSkills`, hides conflicting crafting panels, populates the recipe-book bitmask/filter view, and refreshes visible recipe controls. |
| `201` | `0x00473470` | `OP_KnowledgebaseResponseMsg` | Resolves the `Help` UI and forwards the knowledgebase response payload to the Help window response helper. |
| `203` | `0x00473490` | `OP_CSTicketInfoMsg` | Resolves the `Help` UI and forwards the ticket-info list to the Help ticket-details helper. |
| `205` | `0x004734b0` | `OP_CSTicketCommentResponseMsg` | Resolves the `Help` UI and forwards the ticket-comment list to the Help ticket-comments helper. |
| `209` | `0x004734d0` | `OP_CSTicketChangeNotificationMsg` | Resolves the `Help` UI and forwards the compact ticket-change notification to the Help notification helper. |
| `211` | `0x00485090` | `OP_KnownLanguagesMsg` | Applies the byte list as known-language additions or removals depending on the trailing flag at message `+0x10`. |
| `219` | `0x0046c5f0` | `OP_UpdateClientPredFlagsMsg` | Uses the trailing flag at message `+0x08` to toggle local player/client prediction state through the movement helpers. |
| `220` | `0x0046c620` | `OP_ChangeServerControlFlagMsg` | Single-byte server-control flag row; updates the local player flag at `+0x148` and resets control/prediction state when the flag clears. |
| `229` | `0x00461380` | `OP_ExamineInfoRequestMsg` | Switches on request type byte `+0x04` and routes the conditional payload at `+0x08`/`+0x10` through the ExamineManager request helpers. |
| `230` | `0x004a3180` | `OP_QuickbarInitMsg` | Iterates decoded quickbar entries and installs hotkeys through `0x0049cfe0`; cases translate client hotkey button types into internal quickbar action IDs. |
| `232` | `0x004b0270` | `OP_MacroInitMsg` | Logs `m_processMacroInitMsg`, iterates macro entries, and installs each macro through `0x004ae9f0` while bracketing the update with `DAT_00e712c5`. |
| `235` | `0x0049c660` | `OP_LevelChangedMsg` | Displays adventure/tradeskill level-up or level-down UI text and plays the matching sound/effect based on the level delta and type byte. |
| `236` | `0x00474e50` | `OP_DisplayWarningMsg` | Passes the rich warning payload to `0x00687520`; the body is not the compact encounter-broken string. |
| `237` | `0x0049c990` | `OP_EncounterBrokenMsg` | Formats the retained "Encounter Broken!" UI text around the string payload and triggers the encounter-broken visual path. |
| `238` | `0x00474ec0` | `OP_OnscreenMsgMsg` | Calls `0x00687420` with the `WS_OnScreenMsg` shape: type byte, text, message type, size, and RGB color bytes. |
| `239` | `0x00486c60` | `OP_ModifyGuildMsg` | Consumes the three one-byte-length string fields and derives the command/value triplet either from the optional vector at object `+0x34..+0x38` or from fallback bytes at object `+0x40`; then posts the modify-guild UI/action request through `0x00443e50`. The EQ2Emu numeric `WS_ModifyGuild` status-delta body is gated to 562+ rather than sent malformed to v546. |
| `242` | `0x00472e00` | `OP_GuildEventAddMsg` | Resolves the guild UI and forwards the event-add payload to the guild event display/cache helper. |
| `243` | `0x00472de0` | `OP_GuildEventActionMsg` | Resolves the guild UI and forwards the guild-event action payload to the event action helper. |
| `244` | `0x00472da0` | `OP_GuildEventListMsg` | Resolves the guild UI and forwards the counted guild-event id list plus flags to the event-list helper. |
| `246` | `0x00472dc0` | `OP_RequestGuildInfoMsg` | Resolves the guild UI and forwards the guild-info payload to the request/info helper. |
| `248` | `0x00472e40` | `OP_GuildBankActionMsg` | For guild-bank action status `0`, clears the cached first guild-bank tab snapshot at world-data `+0x32b1` and refreshes the bank UI; status `1` hides/refreshes the bank UI without clearing the snapshot. |
| `252` | `0x00472ea0` | `OP_GuildBankUpdateMsg` | Uses the bank/tab byte at object `+0x08`, decodes the size-prefixed packed blob at `+0x09` against the cached `0x2d8`-byte tab snapshot at world-data `+0x32b1 + tab * 0x2d8`, notifies on changed nonzero item ids, then copies the new snapshot back. |
| `253` | `0x00472e20` | `OP_GuildBankEventListMsg` | Resolves the guild UI and forwards the counted guild-bank event id list to the bank event-list helper. |
| `255` | `0x00474f40` | `OP_RewardPackMsg` | Reads the status byte at message `+0x08`; nonnegative values use direct reward-pack helpers, while the negative path resolves the `RewardPack` UI and applies the payload through `0x006a50b0`. |
| `274` | `0x004754f0` | `OP_MailGetHeadersReplyMsg` | Logs `Received mail headers reply msg...`, passes the header-list message to the mail UI path when the mail window exists, and refreshes/enables the mail screen. The message count is a `u8` in this client. |
| `275` | `0x00475550` | `OP_MailGetMessageReplyMsg` | Logs `Received mail message reply msg...`; updates the mail header/read state from message offsets `+0x08` and `+0x50`, then forwards the full message record to the open mail-message UI and refreshes the screen. |
| `276` | `0x004755c0` | `OP_MailSendMessageReplyMsg` | Logs `Received send mail message reply msg...`; when the mail UI exists, calls the send-mail reply handler to close/reset the compose flow. |
| `281` | `0x0064e960` -> `0x006e39b0` | `OP_WaypointReplyMsg` | GameScene first resolves the `Waypoint` UI object, then rebuilds the waypoint tree from the counted entries. The UI groups records by the category byte at record offset `+0x14`, adds child entries by id at `+0x18`, and applies the trailing waypoint state at message object `+0x10`. |
| `282` | `0x004753f0` | `OP_WaypointSelectMsg` | Resolves the `Waypoint` UI and applies the selected waypoint id through `0x006e2e30`, updating selection, buttons, and current waypoint UI state. |
| `283` | `0x0064e960` -> `0x006e3d70` | `OP_WaypointUpdateMsg` | Reuses the `Waypoint` UI lookup, then applies an incremental waypoint list. An empty update clears/removes existing UI state; nonempty updates rebuild affected category and child entries before applying the trailing waypoint state. |
| `285` | `0x00475430` | `OP_ShowZoneTeleporterDestinationsMsg` | Resolves the `ZoneTeleporter` UI and rebuilds the destination list through `0x006ed4f0`, including destination names, ids, and metadata records. |
| `289` | `0x004afb50` | `OP_GuildMembershipResponseMsg` | Iterates `0xcc`-byte member records from object `+0x0c..+0x10`, opens/refreshes guild UI state when a record matches the local character name, and refreshes the guild membership view for trailing tab/index `0`. |
| `290` | `0x004a1600` | `OP_LeaveGuildNotifyMsg` | Updates or removes the local/guild-tab member state for the trailing tab/index, clears the local guild cache when the local character leaves, and refreshes guild/guild-bank UI state. |
| `291` | `0x004ab080` | `OP_JoinGuildNotifyMsg` | Deserializes the joined member record, updates local guild membership caches when the record matches the local character, and refreshes guild/guild-bank UI state for the trailing tab/index. |
| `293` | `0x00486fc0` | `OP_BioUpdateMsg` | Routes the biography string at message `+0x0c` into the `InspectPlayer` UI and refreshes the panel. |
| `294` | `0x00475450` | `OP_QuestReward` | Uses the wrapper flag at message `+0x04` to choose widget/reward handling or the `InspectPlayer` reward UI path for the payload at `+0x08`. |
| `299` | `0x004734f0` | `OP_CsCategoryResponseMsg` | Resolves the `Help` UI and forwards the category tree to the Help category-list helper. |
| `300` | `0x00473980` | `OP_KnowledgeWindowSlotMappingMsg` | Passes the counted `(spell_id, slot_id)` mapping vector at object `+0x04` to both the knowledge-window and hotkey/slot subsystems. This is the active spell-slot mapping row for this client. |
| `301` | `0x00473510` | `OP_LFGUpdateMsg` | Resolves the `GroupMembers` UI and applies the two-byte LFG state through its virtual update method. |
| `306` | `0x00473540` | `OP_PromoFlagsDetailsMsg` | Resolves the `Claim` UI, loads the counted claim/promo item list, and shows the no-more-items system text when the list is empty. |
| `314` | `0x004739a0` | `OP_UpdateRaidMsg` | Decodes the packed raid delta at object `+0x04` against the cached `0xcdc`-byte raid sheet at world-data `+0x13bc`, copies the decoded sheet back, updates raid UI state, and refreshes visible raid panels. |
| `315` | `0x00473a70` | `OP_UpdateArenaMsg` | Decodes the packed arena delta at object `+0x04` against the cached `0x1219`-byte arena sheet at world-data `+0x2098`, copies the decoded sheet back, and updates the arena UI if it is present. |
| `317` | `0x00473650` | `OP_TitleUpdateMsg` | Resolves the `InspectPlayer` UI and applies the title update there. |
| `323` | `0x0048eba0` | `OP_TrackingUpdateMsg` | Switches on mode byte `+0x04`: stop/search/start modes print tracking status text, while mode `2` rebuilds the tracking result list from the counted record vectors. |
| `327` | `0x004b304b` | `OP_AdvancementRequestMsg` inline case | Copies the two `u32` halves of the 8-byte body from message `+0x08/+0x0c` into global advancement-request state. |
| `328` | `0x00474e20` | `OP_MapFogDataInitMsg` | Logs `m_processMapFogDataInitMsg` and initializes the map-fog subsystem. The recovered body is the compact two-list fog-data form, not PacketParser's numeric row label. |
| `330` | `0x0046c4e0` | `OP_CloseGroupInviteWindowMsg` | Empty payload; closes a cached UI window object and clears the stored handle. |
| `334` | `0x0048edd0` | `OP_OfferQuestMsg` | Uses the `RewardPack` path when available; otherwise builds the quest-offer prompt from title, description, level, and class fields before opening the accept dialog. |
| `336` | `0x0046cd10` | `OP_DisplayMailScreenMsg` | Single `u32` body; handler logs the display-mail-screen message. |
| `345` | `0x004afc50` | `OP_FlightPathsMsg` | Nested route list. The handler consumes route coordinate triplets and computes segment lengths. |
| `366` | `0x00475c30` | `OP_AuctionCharacterReply` | Handles Station Exchange character-auction response codes, clears the `Exchange` UI when present, and displays the matching failure text for nonzero status values. |
| `374` | `0x0049db10` | `OP_GetAuctionAssetIDReplyMsg` | Status byte `0` enters the Station Exchange asset-id success path; nonzero status values display Station Exchange eligibility/error text. |
| `376` | `0x00475c00` | `OP_DisplayExchangeScreenMsg` | Empty payload; resolves the `Exchange` UI, opens/refreshes it, and marks the exchange screen visible. |
| `378` | `0x004b26f0` | `OP_EqHearChatCmd` | Applies the Eq command grouped-list header/helpers, then routes the payload through `ArenaMain` when that UI is present. |
| `379` | `0x00473870` | `OP_EqDisplayTextCmd` | Routes the short-entry text command list through `ArenaMain`. |
| `380` | `0x004738b0` | `OP_EqCreateGhostCmd` | Switches on the action byte and dispatches create/update ghost state through `ArenaMain`. |
| `381` | `0x004b30d4` | `OP_EqCreateWidgetCmd` inline case | Resolves `ArenaMain` and applies the widget/visual-state payload through `0x0054caf0`. |
| `382` | `0x00479c40` | arena zone/status list | Prints the counted arena-zone list to chat/status output; the source-order `OP_EqCreateSignWidgetCmd` label is provisional for this handler-backed row. |
| `383` | `0x004a9130` | `OP_EqDestroyGhostCmd` | Routes the three-id ghost/widget command to several Arena UI helpers, including `ArenaMain`, `ArenaScore`, `ArenaRespawn`, and `ArenaRevive` paths when present. |
| `389` | `0x00473920` | `OP_EqHearSpellInterruptCmd` | Applies the visual-list payload to Arena score/respawn UI helpers. |
| `390` | `0x004b0d20` | `OP_EqHearSpellFizzleCmd` | Switches on the action byte and updates arena visual/revive/crowd/sound state, including ArenaRevive visibility paths. |
| `393` | `0x004ab3e0` | `OP_EqCreateListBoxCmd` | Updates the ArenaScore list-box data from the counted `u32,string8` entries. |
| `398` | `0x004b31a6` | empty Arena UI command | Inline GameScene case that resolves `ArenaMain` and toggles player/UI control state through `0x006e7230`. |
| `399` | `0x004b31d6` | empty Arena UI command | Inline GameScene case that resolves `ArenaMain`, verifies visibility, and toggles Arena UI control state through `0x006e63b0`. |
| `400` | `0x004b3215` | `OP_EqHearDrowningCmd` inline case | Resolves `ArenaRevive` and routes the single `u32` body into the `KilledByText`/revive text update path. |
| `406` | `0x0047a810` | `OP_DisplayEventMsg` | Prints display-event text, optionally routes the onscreen-message fields to `OnscreenMessage`, and executes the trailing string command list. |
| `407` | `0x004a3740` | `OP_PrePossessionMsg` | Empty payload; snapshots the 10x12 possession/control-state table, marks possession active, and refreshes dependent UI state. |
| `408` | `0x0049df30` | `OP_PostPossessionMsg` | Empty payload; restores the possession/control-state table, refreshes dependent UI state, and clears the possession-active flags. |
| `409` | `0x00473610` | `OP_HouseItemsDetailsMsg` | When the house-items UI exists, forwards the item detail list at message `+0x04` to `0x005ccda0`, resets the selection/filter state, and refreshes UI visibility if the list is nonempty. |
| `410` | `0x004892b0` | compact `OP_HouseItemsList` id list | `u8 count` plus `u32` ids copied into `GameScene + 0x37a4` house-item state. v546 XML now sends this compact id-list body instead of the older full `WS_HouseItemsList` row. |

Coverage audit: all `130` nondefault `GameScene` dispatch cases are now represented in the handler table above, including inline cases that do not call a separate helper.

### OP_ClientCmdMsg Nested Dispatch

`OP_ClientCmdMsg` type `50` is an envelope, not a single flat payload. Handler
`0x004ae310` attaches the body as a `VeMemoryStream`, repeatedly calls the
generic `VeType` object deserializer, then calls the nested object's vtable
`+0x0c` method to obtain a second-level dispatch id. Static initializer scans
show these dispatch ids are nested client-command `VeType` ids `411..495`.
They are absent from the primary network-message registry extraction, which
stops at type `410`, and PacketParser row ids/source-order names should not be
substituted blindly. The packet-log decoder now previews and names the first
nested `VeType` tag inside the blob when it is one of the confirmed ids below,
but it intentionally does not try to skip later nested objects until their
individual body layouts are recovered.

Confirmed high-value nested dispatch cases:

| Nested type/dispatch ID | Handler/action | Recovered role | Evidence |
| ---: | --- | --- | --- |
| `0x19b` | `0x00493220` | chat/hear-chat style command | Switches on chat channel/style values, formats tells/broadcast-like text, and routes through chat helpers. |
| `0x19c` | `0x00494100` | display-text command | Calls the display-text path, optional floating text/UI path, and logs/broadcasts when the command byte is `M`. |
| `0x19d` | `0x004a6640` | create/update ghost entity command | Applies packed entity state, logs `Created ghost`, updates scene ghost lookup/state, and refreshes active scene objects. |
| `0x19e` | `0x004a6cf0` | create widget command | Calls the ghost creation path, resolves widget ids/state, and logs `Create widget %s entityID=%u`. |
| `0x19f` | `0x004a6d60` | create sign widget command | Extends the widget creation path with sign/widget-specific state before scene insertion. |
| `0x1a0` | `0x00486880` | destroy ghost command | Logs `Destroyed ghost`, removes the entity from scene ghost lookup/state, and refreshes dependent UI state. |
| `0x1a1` | `0x004a6df0` | update ghost command | Reads packed ghost ids and applies compressed or viewer-dependent ghost state updates to existing scene objects. |
| `0x1a2` | `0x0046c6c0` | set-control-ghost command | Logs `Set control ghost cmd...`, optionally changes the control ghost name/id, and updates camera/control state. |
| `0x1a3` | `0x0046c740` | set-POV-ghost command | Logs `Set POV ghost cmd...` and routes the ghost id through the POV helper. |
| `0x1a4` | `0x00495860` | hear-combat command | Resolves attacker/target/proxy ids, formats combat damage/death-blow/resist states, and triggers combat text/effects. |
| `0x1a5` | `0x00489340` | hear-spell-cast command | Walks caster/target spawn ids, spell ids/effect fields, and routes cast visuals through spell/FX helpers. |
| `0x1a6` | `0x00496f40` | hear-spell-interrupt command | Stops active cast effects and emits the `$0 was interrupted!` display-text path for non-local interrupted casters. |
| `0x1a7` | `0x00497130` | hear-spell-fizzle command | Emits local or remote fizzle text, triggers the `fizzle` animation/event path, and routes display text. |
| `0x1a8` | `0x00497400` | hear-consider command | Formats con/faction responses such as `You could kill %s blindfolded.` and routes them to display text. |
| `0x1ab` | `0x0049d9b0` | set-debug-path-points command | Rebuilds selection/path point lists from counted 0x14-byte records and refreshes the path debug UI state. |
| `0x1ad` | `0x00474800` | canned-emote command | Logs unknown proxies for missing spawns, displays the emote text, and plays the referenced animation id. |
| `0x1ae` | `0x004748c0` | state command | Resolves spawn id plus state id and applies the state/animation change when both are valid. |
| `0x1af` | `0x00474900` | play-sound command | Plays the named sound resource and handles `.xmi` resources through the music/XMI path. |
| `0x1b0` | `0x00474960` | play-3D-sound command | Builds positional audio parameters from the command floats and plays the named 3D sound. |
| `0x1b1` | `0x00497db0(..., 0)` | play-voice command | Resolves spawn-localized voice text, suppresses duplicate voice keys, and routes the clip through voice/audio helpers. Serializer `0x0094f190` / deserializer `0x0094f1f4` use `u32 spawn_id`, `string16 mp3`, `u32`, `string16 text/localization`, two `u32` voice keys, and a trailing `u32`; v546 XML now keeps the source `key` fields at the recovered offsets. |
| `0x1b2` | `0x00498150` | hear-drowning command | Emits drowning display text and triggers `ouch_trigger` / `vocal_drown` events. |
| `0x1b3` | `0x00498330` | hear-death command | Builds local/remote death text including `sleeping with the fishes` and pain/suffering death paths. |
| `0x1b4` | `0x00499d10` | group-member-removed command | Displays `You have left the group.` for the local player or `$T left the group.` for another member. |
| `0x1b6` | `0x004989b0` | invite/offer prompt command | Builds group/raid/guild invite prompts with `Accept`/`Decline` buttons and `ui_invite` UI sound/event routing. Serializer `0x0094f31c` / deserializer `0x0094f370` use `u8 type`, inviter `string16`, guild/offer `string16`, `u8` subtype, and optional zone/context `string16`; v546 XML now keeps the source-used `unknown2` byte instead of inheriting the shorter legacy row. |
| `0x1b7` | `0x00499660` | inspect-player result command | Prints `Inspecting`, surname, level, class, title, and equipment lines to the inspect output channel. Serializer `0x0094f3c4` / deserializer `0x0094f42f` use `string16 name`, `string16 surname`, `string16 title`, `u16 level`, `u16 class`, then 22 `string16` equipment-name fields. |
| `0x1b9` | `0x004975d0` | dialog-open command | Opens NPC/dialog UI state, routes optional dialog text through chat/display helpers, and tracks active voice/dialog ids. |
| `0x1ba` | `0x00474210` | dialog-close command | Closes active dialog UI state and clears the tracked voice/dialog id when it matches the closing dialog. |
| `0x1bb` | `0x0068df70` guarded by `0x0064e4e0` | faction update command | Updates the Persona/Factions UI; debug string names `Eq2GuiPersonaWindow.cpp` and formats `faction: name=%s cat=%s id=%u reaction=%d percent=%d`. |
| `0x1bc` | `0x00618190` after `0x00497930` | collection update command | Rebuilds collection journal rows, emits `You completed $0.` / ready-to-turn-in text, and refreshes collection UI state. |
| `0x1bd` | `0x00616d30` after `0x00497930` | collection filter command | Applies collection item filters and item discovery state; emits `No collections include <0 $0.` and duplicate-item collection text. |
| `0x1be` | `0x00617300` after `0x00497930` | collection item command | Marks a collection item found/removed, updates the item tile, and emits `You added <0 $0 to $1`. |
| `0x1bf` | `0x0061d890(..., 1)` after `0x00497930` | quest journal update command | Handles journal full/overflow/completed text, refreshes active/completed quest UI lists, and can send the type `103` quest-focus follow-up. |
| `0x1c0` | `0x0061fc20` after `0x00497930` | quest journal reply/detail command | Applies quest-detail state at object `+0x08`, refreshes the journal entry, and routes optional display state. |
| `0x1c2` | `0x004ab480` | merchant update command | Updates merchant item lists and store helper state from flags at object `+0x14`. |
| `0x1c3` | `0x00474370` | store update command | Routes the store inventory/list body through store UI helpers and visibility/update flags. |
| `0x1c4` | `0x004ab5d0` | player trade update command | Applies trade-side coin/item state, toggles trade UI control flags, and updates cached trade totals. |
| `0x1c5` | `0x0049a1a0` | help-path command | Copies glow-path waypoints and destination coordinates, computes path distance, and displays `Failed to find a path.` on empty paths. |
| `0x1c6` | `0x00474a60` | help-path-clear command | Clears active path state or routes through the UI clear helper when the help/path window is available. |
| `0x1c7` | `0x00474720` | bank update command | Resolves the `Bank` UI page and applies bank coin/display state through bank-window helpers. |
| `0x1c8` | `0x00474750` | examine-info command | Switches examine body type, updates item/spell/effect examine UI data, and enforces the examine-window limit text. |
| `0x1c9` | `0x004746d0` | loot update command | Resolves the `Loot` UI page and refreshes visible loot lists, timers, display flags, and object/spawn id state. |
| `0x1ca` | `0x0064ce20` + `0x00620040` | junction list command | Resolves `JunctionChoice`, rebuilds `<<$0>> $1` revive/junction entries from 0x48-byte records, and refreshes the junction UI. |
| `0x1cb` | `0x00499ef0` | show-death-window command | Serializer `0x00951f9e` writes revive choices via helper `0x00951eda`: `u16` count, 0x48-byte entries (`u32`, `u8`, three strings, `float`), then a trailing `string16` and two `u8` flags. Handler builds death/revive text such as `YOU HAVE DIED` and Spirit Shard/junction guidance before showing the death UI. |
| `0x1cc` | `0x00485190` | display-spell-fail command | Serializer `0x0094fbf2` writes `u8 error_code`, `u8 spell_type`; the handler switches on the first byte and uses the second byte to choose spell/art/ability text such as `Cannot cast`, `Not prepared`, and knowledge failures. |
| `0x1cd` | `0x00450360` | spell-cast-start command | Serializer `0x0094fcde` writes `float cast_time`, `string16 spell_name`; handler shows the spell casting window, stores active cast text/timer, and updates `Spells_Casting` UI data. |
| `0x1ce` | `0x00447b60` | spell-cast-end command | Serializer `0x0094fd34` writes `u8`, `string16 spell_name`; handler clears active cast state, hides `Spells_Casting`, and closes the spells window when no longer needed. |
| `0x1cf` | `0x004a27b0` | resurrected command | Calls the active death-window/junction cleanup path and clears resurrection/death UI state. |
| `0x1d0` | `0x00494240` | choice-window command | Serializer `0x0094fdba` writes five `string16` fields, `u32 time`, `u8 text_box`, `u8 text_required`, `u32 max_length`, and two trailing `u8` flags; handler copies prompt/accept/cancel strings and routes to the choice-window UI helper. |
| `0x1d1` | `0x0046c670` | set-default-verb command | Resolves the target spawn, applies the command string and range through the mapped-event/default-verb helper, and logs `EqNetClientCmds::REMOVE_MAPPED_EVENT`. |
| `0x1d2` | `0x00473fb0` -> `0x005f37b0` | instruction-window-close command | Resolves the `Instruction` UI and clears active instruction state, hides the window, and resets timer/progress state. |
| `0x1d3` | `0x005f24e0` guarded by `0x0064dfa0` | instruction-window command | Consumes the large instruction payload, localizes text, opens instruction UI state, and stores voice/timer/progress fields. |
| `0x1d4` | `0x00474000` | instruction-window-goal command | Resolves the `Instruction` UI and marks every task under the selected goal complete. |
| `0x1d5` | `0x00474030` | instruction-window-task command | Serializer `0x0094ffa4` writes `u8 goal_num`, `u8 task_num`; handler resolves the `Instruction` UI and marks that goal/task pair complete. |
| `0x1d6` | `0x00474060` | enable-game-event command | Serializer `0x0094fffe` writes `string16 event_name`, `u8 enabled`; handler resolves the event name and routes to the game-event enable helper. |
| `0x1d7` | `0x00474090` | show-window command | Serializer `0x00950054` writes `string16 window`, `u8 show`; handler builds and executes `show_window <name>` or `hide_window <name>` UI commands. |
| `0x1d8` | `0x004740e0` | enable-window command | Serializer `0x009500aa` writes `string16 window`, `u8 enabled`; handler builds and executes `enable_window <name>` or `disable_window <name>` UI commands. |
| `0x1d9` | `0x00474130` | flash-window command | Serializer `0x00950100` writes `string16 window`, `float flash_seconds`; handler builds and executes `flash_window <name>` with optional duration. |
| `0x1da` | `0x0049a350` | hear-play-flavor command | Resolves local/remote display paths, reuses voice playback and chat formatting helpers, and routes flavor text/audio. Serializer `0x00950156` / deserializer `0x00950204` match the v546 `WS_PlayFlavor` row: two ids, six `string16` fields, two voice-key `u32`s, language, and understood flag. |
| `0x1db` | `0x00494410` | update-sign-widget command | Serializer `0x0094ec2f` writes `string16 title`, `u32 spawn_id`; handler resolves `SignBubble` and refreshes sign bubble UI content for the referenced widget. |
| `0x1dd` | `0x00486b80` | show-book command | Resolves `Eq2GuiBookWindow` / `Book_%s`, copies book title/type/page fields, and opens the book UI. Serializer `0x00950440` / deserializer `0x00957bf5` use `u32 spawn_id`, title/type/cover `string16` fields, two one-byte flags, then a counted page array whose entries begin with page text and optional image metadata. v546 XML now keeps the cover page outside the counted page array. |
| `0x1de` | `0x00489620` | questionnaire command | Serializer `0x009504d4` writes `u32`, nine `string16` fields, and three trailing `u8` fields; handler resolves the `Questionnaire` UI and copies the questionnaire body plus response option strings before opening it. |
| `0x1e0` | `0x00496a50` | hear-heal command | Resolves healer/target ids, formats heal text/effects, and routes heal numbers through display/combat visual helpers. Serializer `0x0095066c` / deserializer `0x009506d1` write `u32 caster`, `u32 target`, `u16 heal_amt`, `string16 spellname`, and two trailing `u8` fields; v546 XML now includes the second byte. |
| `0x1e1` | `0x0048e880` | chat-channel-update command | Logs `Received chat channel update msg...`, joins/leaves channel ids, updates channel UI state, and refreshes chat state. Serializer `0x00950736` / deserializer `0x00950761` use only `u8 action` and `string16 channel_name`; v546 XML now omits the later `player_name` tail and keeps the legacy three-field row at `562+`. |
| `0x1e2` | `0x00491520` | who-channel-query-reply command | Formats `/whochannel search results`, player result rows, capped-result text, and sends output to the display text channel. Serializer `0x0095206c` / deserializer `0x0095ba57` match `WS_WhoChannelQueryReply`: `string16 channel_name`, `u8 status`, `u8 player_count`, then player-name strings. |
| `0x1e3` | `0x00473c70` | available-world-channels command | Applies the channel list body, refreshes chat channel state, and updates available-world-channel UI data. Serializer `0x00952107` / deserializer `0x0095bacb` match the v546 `WS_AvailWorldChannels` counted `string16` list; the later `562+` row keeps the extra per-channel byte. |
| `0x1e4` | `0x004a25d0` | update-target command | Resolves a spawn id and calls the target update helper with the target short id or `0xffff` when missing. Serializer `0x0095078c` / deserializer `0x009507a4` use a single `u32`; v546 XML now omits the inherited trailing byte. |
| `0x1e5` | `0x00474550` | consignment-items command | Resolves the `Market` UI and refreshes broker/consignment item data. |
| `0x1e6` | `0x004742c0` | start-broker command | Resolves the `Market` UI, opens broker state, sets broker ids/flags, and refreshes market controls. |
| `0x1e7` | `0x0064dca0` + `0x00652960` | map-exploration command | Resolves `Map` and iterates exploration/map region records, marking map UI state dirty when records change. |
| `0x1e8` | `0x00474600` | store-log command | Resolves the `Store` UI and applies store log/range/filter state through store UI helpers. |
| `0x1e9` | `0x00473f30` | spell-move-to-range-and-retry command | Checks spell movement capability and routes spell id/range/target fields to the move-and-retry helper. |
| `0x1ea` | `0x00491b30` | update-player-mail command | Resolves `Mail`, handles add/delete/read/unread cases, plays `ui_mail_deleted`, and displays unread-mail notification text. |
| `0x1eb` | `0x004a6230` | `VeArenaResultsCmd` | Resolves `ArenaResults`, rebuilds title/team/player result arrays, and refreshes Arena UI state. Serializer `0x009521d5` writes `u8 title_count`, title strings, `u8 subtitle_count`, subtitle strings, then two `u16`-counted record arrays: team records of size `0x28` and player records of size `0x24`. |
| `0x1ec` | `0x005b3360` guarded by `0x0064e660` | `VeGuildUpdateCmd` | Resolves `Guild`, updates per-tab/list item state, and creates/removes guild UI rows from command records. Serializer `0x009508d4` writes `u8 mode`, fixed ids/flags, two string fields, and trailing `u64`/`u32` fields used by the guild row helper. |
| `0x1ed` | `0x0059f810` guarded by `0x00605630` | `VeGuildBankUpdateCmd` | Resolves `GuildBank`, applies item display data, and refreshes the matching guild-bank slot widget. The tiny serializer at `0x00950a0c` writes a `u32` slot/id followed by a polymorphic item-display payload at object offset `+0x08`; the deserializer at `0x00950a34` mirrors that layout. |
| `0x1ee` | `0x00479fa0` | `OP_EqHearSpellNoLandCmd` | Walks target spawn ids and stops/cleans up the referenced spell effect id on each resolved target. The serializer/deserializer layout matches EQ2Emu's `WS_SpellNoLand`: `u32 spawn_id`, `u16 count`, `count * u32 target`, `u32 spell_id`. |
| `0x1ef` | `0x004745d0` | `OP_Lottery` | Resolves `Lotto`, copies lotto/junction state, plays `ui_lotto_spin`, and refreshes lotto UI entries. The serializer/deserializer layout is twelve `u32` digits, matching EQ2Emu's `WS_Lottery`. |

The complete nested switch spans dispatch ids `0x19b..0x1ef` with gaps. Unknown
dispatch values hit the `Unknown client cmd %d` log in `GameScene.cpp` line
`0x2780`.

EQ2Emu implementation note: `PacketStruct::GetOpcodeValue` now overrides the
confirmed v546 `OP_ClientCmdMsg` `OpcodeType` names above to these nested
`VeType` ids before falling back to the opcode table. The fallback remains in
place for unconfirmed client-command rows and other client versions.
These nested `VeType` ids are distinct from the registered top-level packet ids,
so commands such as `OP_EqCreateWidgetCmd` use ids `414..417` inside
`OP_ClientCmdMsg` even though nearby source-order rows also appear as top-level
registry ids `381..384`.
Likewise, the v546 nested switch maps id `457` to `OP_EqUpdateLootCmd`; the
newer `OP_EqCloseWindowCmd` source enum name is not a confirmed v546 nested
dispatch case.
The instruction-window pair is also semantic rather than source-order: v546
uses nested id `466` for `OP_EqInstructionWindowCloseCmd` and id `467` for the
full `OP_EqInstructionWindowCmd` payload. Nested id `476` has a static
initializer but no `OP_ClientCmdMsg` dispatch case in this client build.
EQ2Emu's Lua instruction-window send paths were adjusted to match this:
`InstructionWindowGoal` now uses the `WS_InstructionWindowGoal` struct and
`InstructionWindowClose` sends an empty `OP_ClientCmdMsg`/`OP_EqInstructionWindowCloseCmd`
struct rather than a raw top-level `OP_EqInstructionWindowCloseCmd` packet.
The final tail has another source-order mismatch: v546 nested id `484` is
`OP_EqUpdateTargetCmd`, not `OP_ArenaGameTypesMsg`; the Arena handler is the
later id `491` and appears to be an arena-results UI update rather than a
server XML `OpcodeType` currently used by EQ2Emu. The two tail rows with
matching EQ2Emu XML structs are id `494` (`OP_EqHearSpellNoLandCmd`) and id
`495` (`OP_Lottery`).
`WS_HearThreatCmd` / `OP_EQHearThreatCmd` is not bridged for v546: EQ2Emu's
`ZoneServer::SendThreatPacket` already sends plain chat feedback to clients
`<= 561`, and the nearby v546 nested type initializers at ids `425`, `426`, and
`428` do not match the threat struct layout.
`WS_EqTargetItemCmd` / `OP_EqTargetItemCmd` is also not bridged for v546. The
source transmute path can build this `OP_ClientCmdMsg`, but the 2006 client has
no confirmed nested dispatch case for a target-item chooser command and the
v546 PacketParser opcode export has no `OP_EqTargetItemCmd` row. The XML struct
now starts at `ClientVersion="562"` so v546 callers fail closed instead of
sending an unsupported nested command id.

After the confirmed v546 map, a mechanical XML scan now finds zero unconfirmed
`OP_ClientCmdMsg` rows with `ClientVersion <= 546`. The rows below are
deliberately gated to `ClientVersion="562"` or otherwise source-gated so DoF
clients fail closed instead of receiving unsupported nested command ids:

| Row | Source use observed | v546 status |
| --- | --- | --- |
| numeric `514` / `WS_CommandName` | Commented-out `ClientPacketFunctions::SendCommandNamePacket` test/TODO path | XML gated to `562`; no confirmed v546 nested dispatch id. |
| `OP_EQHearThreatCmd` / `WS_HearThreatCmd` | `ZoneServer::SendThreatPacket` | XML gated to `562`; source already sends chat text for clients `<= 561` instead of the ClientCmd packet. |
| `OP_EqTargetItemCmd` / `WS_EqTargetItemCmd` | `Transmute::CreateItemRequest` via Lua `StartTransmute` | Mitigated for v546 by moving the XML struct to `ClientVersion="562"`; no confirmed nested dispatch id. |
| `OP_AchievementUpdateMsg` / `WS_AchievementUpdate` | `Client::SendAchievementUpdate`, but current login call sites are commented out | XML gated to `562`; PacketParser's numeric row is not a nested `VeType` dispatch id. |
| `OP_GuildRecruiting*` / `WS_GuildRecruiting*` | Guild search/details/image commands | XML gated to `562`; PacketParser rows `573..576` are outside the recovered nested dispatch range and the DoF client search strings do not show guild-recruiting UI support. |
| `OP_CharacterCurrency`, `OP_CommitAATemplate`, `OP_JournalQuestStoryline`, `OP_QuestJournalStoryLines`, `OP_PointOfInterest`, `OP_Research`, `OP_VoiceChatServer` | XML/enum definitions only in this source scan | XML gated to `562`; leave unbridged for v546 until a client handler or active send path is proven. |

For root packets, `WS_SkillInfoResponse` and `WS_SkillInfoItemResponse` now also
start at `ClientVersion="562"`. PacketParser's v546 `OP_SkillInfoResponse`
row is id `479`, outside the recovered 2006 root registry, and a root
serializer/deserializer shape scan did not find a simple matching
`u32/u32[/u32] + string8` response body.
`WS_DressingRoom` now also starts at `ClientVersion="562"`: the v546
PacketParser id `474` is not a registered 2006 root type, and a root layout scan
did not find a match for the legacy 57-byte dressing-room appearance payload.
`WS_InstanceCreated` now also starts at `ClientVersion="562"`. PacketParser maps
`OP_AvatarCreatedMsg` to raw `42` for v546, but the recovered client row `42` is
the two-byte `OP_RequestCampMsg` request. The only nearby client row,
type `40`, is a fixed `u32/string8/u8/...` payload and does not match the
instance-created array body.
`WS_MarketFundsUpdate`, `WS_OpenCharCust`, and `WS_SubmitCharCust` now start at
`ClientVersion="562"` as well. These legacy source rows have no PacketParser
v546 opcode row and no matching recovered root registry entry in the DoF client;
the nearby character-customization/reskin rows are different login/profile
messages, not the world `SubmitCharCust` body.
`WS_ClearForLanding` and `WS_CharacterHousingList` also start at
`ClientVersion="562"`; their source call sites are already guarded to
`GetVersion() > 561`, and no DoF root row has been recovered for either opcode.
`WS_RequestGuildEventDetails` starts at `ClientVersion="562"` because the
PacketParser v546 id `244` is the confirmed `OP_GuildEventListMsg` row, and no
DoF row has been recovered for the member-point-history details body.
`WS_AdventureList` now starts at `ClientVersion="562"` as well. Normal login,
zone, level-up, and AA command call sites were already gated to `version > 561`;
`MasterAAList::DisplayAA` and `GetAAListPacket` now enforce the same guard before
building an `OP_AdventureList` packet.
`WS_UpdateTitle` now starts at `ClientVersion="562"`; the source already guards
the per-spawn `OP_UpdateTitleCmd` broadcast to `> 561`, while v546 still receives
the separate `WS_TitleUpdate` title-list packet.
The remaining placeholder root rows are also fail-closed for DoF:
`WS_BagOptions`, `WS_PetOptions`, and `WS_RecipeBook` now start at
`ClientVersion="562"`, while `WS_CharacterPet` starts at `1188` to match the only
observed sender. `Client::SendPetOptionsWindow` returns without sending for
clients `<= 561`, and `PlayerInfo::serializePet` returns no packet before
version `1188`. v546 recipe delivery continues through the recovered
`OP_UpdateRecipeBookMsg` type `60` path in `Client::SendRecipeList`.
`OP_SpellGainedMsg` is also fail-closed for clients `<= 561`: PacketParser
maps it to type `235`, but recovered DoF type `235` is `OP_LevelChangedMsg`
and no matching spell-gained root row has been confirmed. v546 spellbook state
continues through `OP_UpdateSpellBookMsg`.

After these gates, a mechanical XML scan finds zero non-`OP_ClientCmdMsg` rows
available to `ClientVersion <= 546` whose PacketParser v546 opcode ids are above
the recovered root registry range (`0..410`).

The actual server send path needs a second check beyond XML validation:
`PacketStruct::serialize()` returns `EQ2Packet(GetOpcode(), ...)`, and
`EQ2Packet::PreparePacket()` performs the final EmuOpcode-to-wire-id lookup.
`EQ2Packet::PreparePacket` and `PacketStruct::GetOpcodeValue` now share a
127-entry v546 root override table for every active exact-or-inherited XML root
row whose recovered DoF wire id is confirmed and drifts from the legacy source
enum or PacketParser v546 name. This includes the exact DoF rows such as
character/spell/recipe/skill updates, loot, tradeskill create/update/process
rows, macro/quickbar, mail replies, raid, map fog, zone teleporter list/select,
tracking, house purchase requests, moveable-object placement, and localized text, plus inherited rows such as `OP_ZoneInfoMsg`
(`29`), `OP_SetRemoteCmdsMsg` (`35`), `OP_RemoteCmdMsg` (`36`),
`OP_GameWorldTimeMsg` (`37`), camp/load-state rows, dialog/quest/house/
entity-verb rows, and the later guild/mail/waypoint rows.
`EQApplicationPacket::GetOpcodeConst` and raw-packet name reporting use the same
root table in reverse, so inbound v546 packets are decoded against the
client-derived ids instead of the shifted database/PacketParser ids.

The table intentionally does not map PacketParser-only ids above the recovered
2006 root registry range, such as PacketParser's `OP_CancelSpellCast` `455` and
`OP_SkillInfoRequest` `478`, until a DoF client send path or registry row is
confirmed. PacketParser's `OP_BuyPlayerHouseMsg` label at client type `126`
is directional drift: type `126` is a one-byte purchase/status response, while
the PurchaseHouse Buy button sends the active `house_id` purchase request as
client type `128`.
PacketParser's `OP_UpdatePositionMsg` label at client type `36` is also a
drifted label: type `36` is the confirmed `OP_RemoteCmdMsg` body, and DoF
movement input is handled through the type `34` `VePredictionUpdateMsg` opaque
prediction buffer path. The visible cancel-style commands audited so far
(`all_purpose_cancel`, `clearabilityqueue`) are local UI/queue helpers and did
not expose a root `OP_CancelSpellCast` send path; PacketParser's type `455`
continues to collide with the confirmed nested `OP_EqUpdateBankCmd` id.

The broad active-v546 root drift audit now reports zero uncovered non-
`OP_ClientCmdMsg` XML rows.

Selected unresolved or outbound-style rows that route to the explicit
GameScene default/unhandled branch in the same jump table:

| Coverage | Types |
| --- | --- |
| explicit GameScene default/unhandled branch | `41`, `42`, `101`, `102`, `103`, `111`, `113`, `135`, `136`, `137`, `145`, `146`, `148`, `149`, `241`, `247`, `339`, `344`, `346`, `347`, `350`, `351`, `352`, `353`, `354`, `355`, `358` |

This is not a complete list of every default-routed type; known client-to-server
or scene-specific rows such as `38`-`40` and `45` can also hit the default branch
when received by GameScene. Direct xref audit note: type `42` and the outbound rows `103`, `135`-`137`,
and `145`-`146` now have send-site evidence documented in the registry table
below, but GameScene still defaults them on inbound dispatch. The other rows in
the list currently only show registry/vtable/type-info references in static
xref scans, not standalone send builders or inbound handler call sites.

Packet serializers/deserializers now confirmed for the login/create/play handoff:

| Type | Client name | Function(s) | Wire layout |
| --- | --- | --- | --- |
| `0` | `OP_LoginRequestMsg` | `0x0091d4cd` serialize, `0x0091d548` deserialize | Four `string16` fields, two `u32` fields, and a trailing client-version `u16`. The serializer emits the trailing version from a global constant; the deserializer stores it at object `+0x5c`. |
| `1` | `OP_LoginByNumRequestMsg` | `0x0091d5b8` serialize, `0x0091d619` deserialize; factory `0x0091b18c`, type-info `0x0046add0` | `u32`, `u32`, `u16`, then 20 raw bytes. PacketParser has an older numbered-login struct here; this is the DoF registered row layout. |
| `2` | `OP_WSLoginRequestMsg` | `0x0091d66f` serialize, `0x0091d746` deserialize | `u32`, three `u16` client-version fields/constants, `u8`, two `string16` fields, three trailing `u8` values, then two `u32` values. The serializer emits the three version fields from globals. |
| `3` | `OP_ESLoginRequestMsg` | `0x0091d806` serialize, `0x0091d8c6` deserialize | Three `string8` fields, two boolean bytes, optional `u32` when the first boolean is false, then three `u16` client-version fields/constants. The serializer emits the three version fields from globals. |
| `4` | `OP_LoginReplyMsg` | `0x0091d96d` primary serialize, `0x0091d9f9` primary deserialize; handler `0x0046c330` | Actual constructed object vtable starts at `0x00d35ab0`. Primary layout is `u8 reply_code`, `string16`, `u8 parental_control_flag`, `8 bytes`, `u32`, `u32 account_id`, `string16`, `u8 reset_appearance`, `u8 do_not_force_soga`. `server/LoginStructs.xml` now has an exact v546 `LS_LoginReplyMsg` row for this primary layout instead of inheriting the 284-era PacketParser tail. Server comments identify common reply codes: `0` accepted, `1` bad username/password, `2` already playing, `6` client version mismatch, `7` no scheduled playtimes, `8` missing account features, `10` EQ2Emu accepted/world-list flag path, `11` client build mismatch, and `12` password update required. The older registry row had captured shared secondary/base functions at `0x0091debc`/`0x0091def6`; PacketParser-style tail bytes after this primary layout are ignored by this client. The GameScene handler advances state only for reply code `0`. |
| `5` | likely `OP_WSStatusReplyMsg` | `0x0091db68` serialize, `0x0091dbcd` deserialize | `u16`, three `u8` values, then two `u32` values. |
| `6` | `OP_WorldStatusChangeMsg` | `0x0091dabc` serialize, `0x0091db12` deserialize; factory `0x0091b1e4`, type-info `0x00915360`; handler `0x004de940`; inline GameScene case `0x004b332d` | `u32 world_id` followed by four `u8` status/load/flag bytes. The character-select dispatcher updates cached world-list entry bytes `+0x40..+0x43`; the GameScene dispatch case accepts but no-ops. |
| `7` | `OP_AllWSDescRequestMsg` | base empty serializer/deserializer | Empty request for the world-list description path. |
| `11` | `OP_CreateCharacterRequestMsg` | `0x0091dd80` serialize, `0x0091ddc5` deserialize | DoF request header is `u8`, `u32`, `u32`, then `CreateCharacterProfile` at object `+0x10`. The shared profile serializer `0x0097cec5` writes `u32 server_id`, `string16 name`, six one-byte character fields, one byte from profile `+0x34`, then `EqCustomizationData` v4. |
| `12` | `OP_CreateCharacterReplyMsg` | `0x0091debc` serialize, `0x0091def6` deserialize; handler `0x004db7a0` | Shared `u32`, `u8`, `string16` layout: object `+0x04` account id, `+0x08` response, `+0x0c` character name. Response `1` succeeds; response codes `2..14` are no-server, pending request, max characters, invalid race/class/gender, name length, letters-only, reserved/naughty, name taken, overloaded, unknown/rerun-patcher, and Station Exchange feature mismatch. |
| `13` | `OP_WSCreateCharacterRequestMsg` | `0x0091df30` serialize, `0x0091df69` deserialize | World-server create request wrapper: `u32 account_id`, shared `CreateCharacterProfile` at object `+0x08`, trailing `u8` at object `+0x1d4`. |
| `14` | `OP_WSCreateCharacterReplyMsg` | `0x0091dfa2` serialize, `0x0091dff9` deserialize | `u32`, `u8 response`, `string16 name`, `u32`, trailing `u8` from object `+0x09`. This is a registered client message class but is not the source2 login-world `ServerOP_CharacterCreate` payload. |
| `15` | `OP_ReskinCharacterRequestMsg` | `0x0091de0a` serialize, `0x0091de63` deserialize | `u32`, `u32`, shared `CreateCharacterProfile` at object `+0x0c`, then two trailing `u8` values at object `+0x1d8` and `+0x1d9`. |
| `16` | `OP_DeleteCharacterRequestMsg` | `0x0091e050` serialize, `0x0091e099` deserialize | `i32 character_id`, `i32 server_id`, `i32 unknown`, `string16 character_name`. |
| `17` | `OP_DeleteCharacterReplyMsg` | `0x0091e0e2` serialize, `0x0091e12b` deserialize; handler `0x004db650` | `u8 response`, `i32 server_id`, `i32 character_id`, `string16 character_name`. The character-select delete handler only branches on response byte `+0x04`: `1` success, `3` character not found, and other values generic delete failure. `server/LoginStructs.xml` now has a v546 override that omits the legacy `account_id` and `max_characters` tail fields. |
| `18` | `OP_PlayCharacterRequestMsg` | `0x0091dc32` serialize, `0x0091dc7b` deserialize | `i32 character_id`, `i32 server_id`, `u8 unknown`, `string16 character_name`. The handler at `0x004d5020` populates character/server ids from the selected character-list entry before sending. `server/LoginStructs.xml` now has an exact v546 `LS_PlayRequest` row instead of treating the string length bytes as extra unknown bytes. |
| `19` | `OP_PlayCharacterReplyMsg` | `0x0093a0a5` serialize, `0x0091dcc4` deserialize; handlers `0x004d5460`, `0x004e2f70` | `u8 response`; if response is `1`, then `string8 address`, `u16 port`, `u32 account_number`, `u32 passcode`. Failure replies are response-only in the client serializer/deserializer, and `LoginServer::Client::SendPlayFailed` now sends only that byte for DoF-era clients. The login/autologin handler maps codes `2`, `3`, `4`, `5`, `6`, `7`, `9`, and `10`; the character-select handler maps codes `3`, `4`, `5`, `6`, `7`, `10`, and `11`. |

The create-character customization helper at `0x0097d68d` writes version byte
`4`, then `race_file`, five float color triplets, and immediately the
`hair_file` string. The 26-byte placeholder present in the legacy XML/
PacketParser-era `CreateCharacter` struct is not emitted by this DoF 546 client
before `hair_file`; the v546 `server/CommonStructs.xml` row now removes it.
`CreateCharacterProfile` writes six one-byte fields at profile `+0x2d..+0x32`,
one byte at `+0x34`, and then `EqCustomizationData`; the v4 customization
serializer `0x0097d68d` writes its own version byte `4` before `race_file`.
This matches source2's three-byte legacy prefix after the five exposed
race/gender/deity/class/level bytes.

World and character-select list serializers use the actual message-object
vtables from their constructors, not the type-info vtable in the registry CSV:

| Type | Message | Function(s) | Wire layout |
| --- | --- | --- | --- |
| `8` | `OP_WorldListMsg` / world list | `0x00927eff` serialize, `0x0094443b` deserialize; handlers `0x004d4b10`, `0x004e9ef0` | `u8 count`; each in-memory entry has `0x44` stride and writes `i32 world_id`, `string16 display_name`, `string16 second_name`, four `u8` flags at entry offsets `+0x2c..+0x2f`, `u16 player_count`, `u8 load/development flag`, `u8`, `u8`, `u32 feature/race mask`. The wire entry is variable-length because of the strings. The login path uses it for autologin; the character-select path refreshes the world list UI. |
| `9` | `OP_AllCharactersDescRequestMsg` | base empty serializer/deserializer | No custom payload fields. The login scene sends this request after selecting a world from the cached world list. |
| `10` | `OP_AllCharactersDescReplyMsg` / all characters reply | `0x00928026` serialize, `0x009460b7` deserialize; handlers `0x004d5020`, `0x004ea000` | `u8 count`; each in-memory character entry has `0x1cc` stride and is serialized by `0x00982697`/`0x00982793`, then trailer `i32 account_id`, `i32 -1`, `u16 linked_id_count`, `linked_id_count * u32`, `i32 max_character_count_or_version`, `u8 0`. The wire entry is variable-length because of the strings. The login path uses it to send a play-character request; the character-select path rebuilds the character UI. |

The type `10` character entry fixed fields are: `+0x00 i32 character_id`,
`+0x04 i32 server_id`, `+0x08 string16 name`, `+0x1c u8 race`,
`+0x1d u8 class`, `+0x1e u8 gender`, `+0x20 i32 level`,
`+0x24 string16 zone_name`, `+0x60/+0x64/+0x68/+0x6c/+0x70/+0x74`
six `i32` values, `+0x38 string16`, `+0x4c string16`, `+0x78 i32`,
and a NetAppearance block at `+0x7c` serialized by `0x008fab1b`.
The NetAppearance writer emits version byte `0x0b` and a 337-byte total wire
block including the version byte. After model/skin/eye it writes `0x1b`
equipment-style groups. Groups `23..26` occupy the old XML positions for
hair, facial hair, chest, and legs. The late v11 tail is object-offset ordered:
primary morph bytes, mount and primary hair colors, unknown/flag fields,
SOGA model/skin/eye, SOGA morph bytes, SOGA hair colors, then SOGA hair and
facial-hair groups. The matching deserializer accepts NetAppearance versions
`0..0x0b`; source2 should emit the `0x0b` layout for this DoF client path.
Helper `0x008fa0e9` default-initializes the `0x150`-byte object and sets
flag bit `0x2` at object `+0x14c`. Helper `0x008fa8f8` is the pre-v8
compatibility copier that mirrors primary model/skin/eye, primary hair and
facial-hair groups, primary morph bytes, and primary hair colors into the
SOGA offsets.

`LoginScene_OnReceiveMsg_Maybe` confirms these login reply rejection codes from
client UI strings: `1` invalid username/password, `2` account already playing,
`6` client version mismatch, `7` parental-control usage limit, and `8` missing
account features for the selected server.

`LoginScene_HandlePlayCharacterReply_Maybe` confirms these autologin
play-character reply codes: `1` success, `2` requested world not found, `3` no
server for the character's zone, `4` game-server rejection, `5` requested
character not found, `6` account in use, `7` server timeout, `9` technical
difficulty loading the character, and `10` Station Exchange feature mismatch.
The character-select play handler at `0x004e2f70` shares the same success body
but maps failures `3`, `4`, `5`, `6`, `7`, `10`, and `11` (`class selected is
not valid`).

`0x004db7a0` confirms create-character reply codes: `1` success, `2` no
servers, `3` request pending, `4` maximum characters, `5` invalid race, `6`
invalid class, `7` invalid gender, `8` name too short/long, `9` non-letter
name, `10` reserved/naughty name, `11` name already taken, `12` overloaded,
`13` unknown/rerun patcher, and `14` missing Station Exchange features.
`0x004db650` confirms delete-character reply code `1` success and `3`
character not found.

The client registry diverges from the extracted PacketParser v546 opcode names
after type `19`. This client has concrete type `20`/`21` server-play messages
and `22` is `VeESInitMsg`; PacketParser has extra rows at `22`-`24` and shifts
`OP_ESInitMsg` to `25`.

| Type | Inferred message | Function(s) | Wire layout |
| --- | --- | --- | --- |
| `20` | `OP_ServerPlayCharacterRequestMsg` | `0x0092a8c9` serialize, `0x0092c9f0` deserialize | `u32`, `u32`, four `string16` values, `u32 count`, `count` repeated `u32`, trailing `u8`. |
| `21` | `OP_ServerPlayCharacterReplyMsg` | `0x0093a142` serialize, `0x0091dd22` deserialize | `u8 response`, `u32` at object `+0x20`; if response is `1`, then `string8`, `u16`, `u32`. |
| `22` | `VeESInitMsg` / `OP_ESInitMsg` | `0x0092ab3e` serialize, `0x009498a8` deserialize | Header fields are `u32`, two `string8`, two `string16`, two `u32`, then a bounded `u32 guild_count` (`<= 1,000,000`). Guild payload entries are variable nested `GuildUpdate` records; after the guild list comes a `u16 character_group_count`, then groups of `u32 id`, `u16 member_count`, and member `u32` ids. |
| `23` | likely `OP_ESReadyForClientsMsg` | base empty serializer/deserializer | No custom payload fields. |
| `24` | likely `OP_CreateZoneInstanceMsg` | `0x0091e174` serialize, `0x0091e1ff` deserialize | Five `string16` values followed by four `u32` values. |
| `25` | likely `OP_ZoneInstanceCreateReplyMsg` | `0x0091e28a` serialize, `0x0091e2b5` deserialize | `string16`, `u8`. |
| `26` | likely `OP_ZoneInstanceDestroyedMsg` | `0x00918c9d` serialize, `0x00918cb3` deserialize | `string16`. |
| `27` | likely `OP_ExpectClientAsCharacterRequest` | `0x0092ac97` serialize, `0x0092cabb` deserialize | `u32`, `u32`, `string16`, `u8 optional-block flag`, three `string16`, four `u32`, optional four-`u32` block when the flag is nonzero, another `u32`, `string16`, `u32 count`, `count` repeated `u32`, and a trailing `u8`. |
| `28` | likely `OP_ExpectClientAsCharacterReplyMsg` | `0x0091e2e0` serialize, `0x0091e3a3` deserialize | `u8`, `u32`, `u32`, `u16`, `u32`, `u32`, `string16`, four `u32` values, and two trailing `u8` flags. This fills the shifted PacketParser row `OP_ExpectClientAsCharacterReplyMs` before zone info. |
| `29` | `OP_ZoneInfoMsg` | `0x0092ae18` serialize, `0x0094813f` deserialize; handler `0x004ad910` | Top-level order is two `string8`, two `u32`, `string8`, `u32`, six more `string8`, 12 raw bytes, `u16`, five `u8`, slideshow helper, four `u32`, then a `u32` count of `string16`/`u16` entries. The slideshow helper writes two `u32`, a `u8 slide_count`, and slide records with six `u32`, two `string8`, a `u64`, and nested four-`u32` rows. |
| `30` | unnamed counted `u16` list | `0x0092b01b` serialize, `0x0092e5e4` deserialize | `u32 count`, then `count` repeated `u16` values. |
| `31` | `OP_DoneSendingInitialEntitiesMsg` | base empty serializer/deserializer; handler `0x00472d00` | Empty payload. The GameScene handler logs `Done sending initial entities msg`, calls the loading/UI transition helper, and sets GameScene state `0x13`. This client row is not PacketParser's v546 source-order `DoneLoadingZoneResources` label. |
| `32` | likely `OP_DoneLoadingZoneResourcesMsg` | base empty serializer/deserializer | Empty client-to-server style loading-state row; GameScene does not install an inbound handler for this row. The loading state machine logs `Done loading entity resources` at `0x004ad4a6` and sends the row with type field `2` from vtable `0x00cd7cd8`; EQ2Emu keeps routing this source opcode to `SendZoneSpawns()` because it is the client's ready-for-initial-spawns signal. |
| `33` | `OP_DoneLoadingUIResourcesMsg` | base empty serializer/deserializer; inline GameScene case `0x004b27b3` | Empty loading-state row; the loading state machine sends the row with type field `3` from vtable `0x00cd7cec` after the UI/pending-examine resource wait, then waits for the server to echo the same row. The GameScene dispatch case sets global loaded-state flag `DAT_00f17428` and lets the client advance. PacketParser v546 labels this area as entity/resources, but the send/wait evidence ties type `33` to the UI-done ack. |
| `34` | `VePredictionUpdateMsg` / `OP_PredictionUpdateMsg` nested buffer | `0x0091e466` serialize, `0x0091e4b8` deserialize | `packed_u16 prediction_subtype_id`, `u32`, `u32 blob_size`, then `blob_size` raw bytes; deserializer rejects blob sizes `>= 0x401`. The v546 root override table maps this handled movement-prediction row to client type `34`. |
| `35` | `OP_SetRemoteCmdsMsg` | `0x0092811e` serialize, `0x00946229` deserialize; handler `0x004b0090` | Two `u16` counts, each followed by count entries from 0x14-byte in-memory arrays serialized as `string8`-style fields. The handler logs `Recieved remote command table (%d commands)`, installs command callbacks, and registers aliases from the secondary list. `RemoteCommands::serialize` now emits an explicit zero secondary-count for v546 instead of relying on the old trailing NUL byte. |
| `36` | `OP_RemoteCmdMsg` | `0x0091e538` serialize, `0x0091e563` deserialize | Matches `WS_RemoteCmdMsg`: `u16 command_handler`, `string16 arguments`. |
| `37` | `OP_GameWorldTimeMsg` | `0x0091e58e` serialize, `0x0091e621` deserialize; handler `0x0046c420` | Matches client 546 `WS_GameWorldTime`: `u16 year`, then month/day/hour/minute/unknown as five `u8` values. The handler forwards the body at object `+0x04` to the active scene-time path when the 3D scene is ready. |
| `38` | single `string16` message | `0x00918cc9` serialize, `0x00918cdf` deserialize | Single `string16`; likely one of the MOTD-style rows, but not yet tied to a handler. |
| `39` | two `string16` message | `0x00918cf5` serialize, `0x00918d1e` deserialize | Two `string16` values; likely adjacent MOTD-style row, but not yet tied to a handler. |
| `40` | unidentified fixed-field message | `0x0091e6f0` serialize, `0x0091e7ca` deserialize | `u32`, `string8`, `u8`, eight `u32`, two `u8`, `string8`, trailing `u8`. This does not match the variable `WS_InstanceCreated`/`OP_AvatarCreatedMsg` array layout. |
| `41` | MOTD/avatar-adjacent row | `0x0091e8a4` serialize, `0x0091e8d1` deserialize | Small `u32`, `u8` payload. PacketParser labels the nearby opcode-number row as `OP_ZoneMOTDMsg`, but this body is not a MOTD string payload and the handler tie-in is still unresolved. |
| `42` | `OP_RequestCampMsg` outbound request | `0x00920d23` serialize, `0x00920d50` deserialize; send builder `0x004b7bf0` | Two `u8` values. The command builder parses the optional `desktop` argument and sends this row with the quit/camp-desktop style flags. This matches EQ2Emu's v546 `WS_RequestCamp` fallback row; the wider camp-char-select request starts at client `562`. GameScene still routes inbound type `42` to the default/unhandled branch. |
| `43` | `OP_CampStartedMsg` / incoming camp-start layout | `0x0091e8fe` serialize, `0x0091e934` deserialize | Matches the shared two-byte camp-start shape: `u8 seconds_or_state`, `u8 camp_desktop`, and EQ2Emu's v546 `WS_Camp` fallback row. The GameScene handler at `0x0048e700` treats this as the server camp-start state. |
| `44` | `OP_CampAbortedMsg` | base empty serializer/deserializer | Handler-backed at `0x00484f80`: empty server message that clears camp preparation state and prints the camp-abort text. |
| `45` | `OP_EnvironmentMapStateMsg` / environment-map state | `0x00918d47` serialize, `0x00918d58` deserialize | Thin wrappers around `0x00991011`/`0x0099119c`: `u32`, five `u8` flags, optional gated `string8`, another flag with optional `u32`, two raw 16-byte blocks, `u32`, three `u8`, `string8`, three `u8`, `u16 count`, and `count` repeated `u32`. The deserializer's gated path references the client string `Updating environment map`. Do not treat this as the simple `WS_Camp` layout or the PacketParser `OP_MapRequest` row. |
| `46` | `OP_WhoQueryReplyMsg` | `0x009281ea` serialize, `0x0093a1e2` deserialize; handler `0x00490090` | Matches client 546 `WS_WhoQueryReply`: `u32 account_id`, `u32 unknown`, `u8 response`; response `2`/`3` carries `u8 count`, up to `cl_who_query_cap` entries of `char[40] name`, level/admin/class/race/flag fields, `char[80] zone`, 28 trailing unknown bytes, and trailing display-zone byte. The handler formats `/who` text output and handles capped/no-result states. |
| `47` | likely `OP_MonitorReplyMsg` | `0x0091e96e` serialize, `0x0091e9aa` deserialize | `u32`, then two raw `0x0c`-byte blocks. The source/PacketParser order places this after `WhoQueryReply`. |
| `48` | likely `OP_MonitorCharacterListMsg` | `0x009282ce` serialize, `0x0093a274` deserialize | `u16 count`, then `count * 0x4c` raw bytes. |
| `49` | likely monitor-list request row | `0x0091e9e6` serialize, `0x0091e9fe` deserialize | Single `u32` payload. The source/PacketParser label is still unresolved. |

EQ2Emu implementation note: the legacy `WS_MapRequest` XML row is only
`string16 zone` plus `u8 unknown` and has no `OpcodeName`; PacketParser v546
lists `OP_MapRequest` separately at opcode `999`. The DoF type `45` wire layout
above is an environment-map zoning-state message, not that map-request struct.
Do not wire the source-side `OP_MapRequest` handler to type `45` without first
finding the client's true map-request sender/serializer. The extracted client
registry has no root type `999`, so the PacketParser `OP_MapRequest` row is not
currently backed by a recovered DoF client message class.
| `50` | `OP_ClientCmdMsg` | `0x0091ea1e` serialize, `0x0091ea55` deserialize; handler `0x004ae310` | `u32 blob_size`, then `blob_size` raw bytes from an object buffer sized around `0x40000`. The handler attaches the blob as a memory stream, repeatedly deserializes nested `VeType` client-command objects, and dispatches nested command ids `0x19b..0x1ef` (`411..495`). The decoder previews and names the first nested `VeType` tag for confirmed ids, but does not walk later commands without per-subtype layouts. |
| `51` | `VeDispatchClientCmdMsg` / `OP_DispatchClientCmdMsg` | `0x0091ec56` serialize, `0x0091ed2a` deserialize | `u8 dispatch_method`; method `0` has a `string8`, method `1` has no method-specific field, method `2` has two `string8` values, methods `3`/`5` have a `u32`, and method `4` has a `string8`; then `string8`, packed `u16` payload size, and raw payload bytes. |
| `52` | `VeDispatchESMsg` / `OP_DispatchESMsg` | `0x0091edfb` serialize, `0x0091eeac` deserialize | `u8 dispatch_method`; method `0` has no method-specific field, method `1`/`4` have a `string8`, methods `2`/`3` have a `u32`; then packed `u16` payload size and raw payload bytes. The deserializer rejects payload sizes above `0x8000`. |
| `53` | likely `OP_UpdateTargetMsg` | `0x0091ef6c` serialize, `0x0091ef84` deserialize | Raw `u16` at object `+0x04`. |
| `54` | likely `OP_UpdateTargetLocMsg` | `0x0091ef9c` serialize, `0x0091efb4` deserialize | Raw `0x0c` bytes at object `+0x04`, consistent with three float coordinates. |
| `55` | `VeUpdateCharacterSheetMsg` / `OP_UpdateCharacterSheetMsg` | `0x0091efcc` serialize, `0x0091f003` deserialize; handler `0x004a1790` | `u32 packed_size`, then an EQ2Emu `Pack()` zero-run/literal stream. Server-side `PlayerInfo::serialize` packs the v546 `WS_CharacterSheet` directly on first send, then XORs future sheets against the previous full sheet before packing. The v546 unpacked sheet is 4897 bytes and includes fixed identity/class/level fields, HP/power/combat stats, 30 spell-effect slots of 19 bytes, 30 maintained-effect slots of 87 bytes, state flags, five group-member slots, pet fields, house/bind zone strings, status points, and guild status. The handler decodes into the cached `0x1321`-byte character sheet. The deserializer rejects packed sizes above `0x8000`. |
| `56` | `VeUpdateSpellBookMsg` / `OP_UpdateSpellBookMsg` | `0x0091f06a` serialize, `0x0091f0b3` deserialize; handler `0x004a6030` | `u16 spell_count`, `u32 packed_size`, then an EQ2Emu `Pack()` zero-run/literal stream. Server-side `Player::GetSpellBookUpdatePacket` builds this from v546 `SubStruct_UpdateSpellBook` records of 27 bytes: `spell_id`, signed `unique_id`, `recast_available`, `type`, `recast_time`, `unknown3`, `unknown4`, signed `icon`, `icon_type`, `icon2`, `charges`, `unknown5`, and `status`. Because `serializeCountPacket` XORs against the previous full spell-book buffer, later packets are packed deltas rather than standalone full records. The deserializer rejects packed sizes above `0x8000`. |
| `58` | `OP_UpdateInventoryMsg` | `0x0091f12c` serialize, `0x0091f187` deserialize; handler `0x004a1dd0` | `u16 item_count`, `u32 packed_size`, EQ2Emu `Pack()` zero-run/literal stream, trailing `u8 equip_flag`. Server-side inventory/equipment serialization calls `serializeCountPacket(version, 1, orig_packet, xor_packet)`, so this body is an XOR delta against the previous full item buffer. The v546 `Substruct_Item` stride is 108 bytes: `unique_id`, `bag_id`, `inv_slot_id`, `menu_type`, `slot_id`, `index`, `icon`, `count`, `level`, `tier`, `num_slots`, signed `item_id`, fixed 64-byte `name`, and 17 trailing unknown bytes. |
| `59` | `OP_AfterInvSpellUpdate` | `0x0091f1e2` serialize, `0x0091f22b` deserialize; factory `0x0091d430` | Handler-backed at `0x004a2420`: `u16 count`, `u32 packed_size`, then raw bytes at object `+0x04`. The handler decodes with `CompressedXOR_Decode_Maybe` into `count * 0x1b` records and refreshes a second spellbook-like state tree at world-data offsets `+0x1348..+0x134c`; type `56` uses the same record stride and helper for the main spellbook vector at `+0x1321..+0x1325`. `server/WorldStructs.xml` now has a v546 override that mirrors `WS_UpdateSpellBook`'s counted `SubStruct_UpdateSpellBook` array instead of inheriting the fixed 21-byte placeholder row. The object allocation is `0x800c`, with count at `+0x8008` and size at `+0x8004`, but the recovered deserializer has no explicit size cap/assertion unlike the adjacent spell/recipe/skill update blobs. |
| `60` | `VeUpdateRecipeBookMsg` / `OP_UpdateRecipeBookMsg` | `0x0091f274` serialize, `0x0091f2c0` deserialize; handler `0x004a19a0` | `u16 recipe_count`; when nonzero, `u32 packed_size` and an EQ2Emu `Pack()` zero-run/literal stream. Server-side `Client::SendRecipeList` builds this with `serializeCountPacket(..., nullptr, nullptr)`, so the packed body is a full array, not a delta. The v546 entry is 12 bytes: `recipe_id`, `recipe_data_crc`, and `unknown` (server fills `0x07005be3`). The handler sends follow-up detail requests for unknown recipe ids. The deserializer rejects packed sizes above `0x50000`. |
| `61` | `VeRequestRecipeDetailsMsg` / `OP_RequestRecipeDetailsMsg` | `0x0091f33c` serialize, `0x009368b7` deserialize | `u32 recipe_count`, then `recipe_count` repeated `u32 recipe_id`; deserializer rejects counts `>= 0x10001`. |
| `62` | `VeRecipeDetailsMsg` / `OP_RecipeDetailsMsg` | `0x0092832a` serialize, `0x0093a2c3` deserialize; handler `0x0048feb0` | `u32 recipe_count`, then v546 `WS_RecipeDetailList` entries of `recipe_id`, `icon`, fixed recipe name/description/book/device strings, book/technique/knowledge/level fields, and `device_id`. Each entry is `0x2d6` bytes; deserializer rejects counts `>= 0x10001`. The handler inserts/updates each recipe-detail record in the client recipe cache and refreshes the recipe book. |
| `63`-`64` | `VeUpdateSkillsMsg` variants; likely `OP_UpdateSkillBookMsg` then `OP_UpdateSkillsMsg` | `0x0091f38e`/`0x0091f450` serialize, `0x0091f3d7`/`0x0091f499` deserialize; type `63` handler `0x004a1c20` | `u16 skill_count`, `u32 packed_size`, EQ2Emu `Pack()` zero-run/literal stream. PacketParser/source order places `OP_UpdateSkillBookMsg` before `OP_UpdateSkillsMsg`, matching client rows `63` and `64`; both wrappers use the same envelope and reject sizes above `0x8000`. The v546 `WS_UpdateSkillBook` record size is 21 bytes: `skill_id`, `type`, `current_val`, `base_val`, `max_val`, `skill_delta`, `skill_delta2`, `display_minval`, `display_maxval`, `language_unknown`. EQ2Emu now packs the v546 records directly with `serializeCountPacket(..., offset=0)` instead of using the old global-byte/trailing-byte shape that does not exist in the 2006 client wrapper. |
| `65` | `OP_UpdateOpportunityMsg` / Heroic Opportunity | wrapper `0x00919100`/`0x00919107`, embedded `0x005c4310`/`0x005c43d0`; handler `0x00473c40` | Wrapper delegates to embedded object `+0x04`. Embedded layout matches `WS_HeroicOpportunity`: `string16 name`, `string16 description`, `u32 id`, three `u8` fields, two `u16` icons, two 4-byte time fields, six `u16` icon slots, and six `u8` counter flags. |
| `67` | `OP_ChangeZoneMsg` | `0x0093a338` serialize, `0x0091f512` deserialize; handler `0x00491d40` | Matches `WS_ZoneChangeMsg`: `u32 account_id`, `u32 key`, `string16 ip_address`, `u16 port`. The handler stores the pending zone endpoint/key, sets the zone-change flag, and sends the ready/ack message before destroying the request object. PacketParser v546 lists this at type `70`. |
| `68` | likely `OP_ClientTeleportRequestMsg` | `0x0091f55b` serialize, `0x0091f573` deserialize | Raw `0x0c` bytes, likely three coordinates. |
| `69` | `OP_TeleportWithinZoneMsg` | `0x0091f58b` serialize, `0x0091f5a3` deserialize; handler `0x004b0240` | Raw `0x0c` bytes, matching the three-float `WS_TeleportWithinZone` shape. The handler logs `Teleport within zone msg` and passes the coordinate vector at object `+0x04` to the teleport helper. |
| `70` | `OP_TeleportWithinZoneNoReloadMsg` | `0x0091f5bb` serialize, `0x0091f5e8` deserialize; handler `0x00473d30` | Raw `0x0c` bytes plus `u32`/float at object `+0x10`, consistent with coordinates plus heading. The handler logs `Teleport within zone no reload msg`, updates the active scene transform without zone reload, and refreshes camera/scene state. |
| `71` | likely `OP_MigrateClientToZoneRequestMsg` | `0x0092b06c` serialize, `0x0092cc41` deserialize | Large zone handoff structure with two leading `u32` values, five `string16` values, four `u32` values, another `string16`, an optional block containing four `u32`, two `string16`, and one `u32`, then `u32`, `string16`, `u8`, counted `u32` list, and two trailing `u32` values. The optional block is controlled by object state, not an explicit wire flag. |
| `72` | likely `OP_MigrateClientToZoneReplyMsg` | `0x0093a3c2` serialize, `0x0091f615` deserialize | `u8`, two `u32`, `string16`, `u16`, `string8`, four `u32`, and two trailing `u8` values. |
| `73` | likely `OP_ReadyToZoneMsg` | base empty serializer/deserializer | No custom payload fields. |
| `74` | likely `OP_RemoveClientFromGroupMsg` | `0x0091f6ca` serialize, `0x0091f7df` deserialize | Ten `u32` values followed by six `u8` flag bytes. Name follows PacketParser v546 order; no v546 XML struct was found for semantic field names. |
| `75` | likely `OP_RemoveGroupFromGroupMsg` | `0x0091f905` serialize, `0x0091f932` deserialize | Two `u32` values. Name follows PacketParser v546 order; no v546 XML struct was found. |
| `76` | likely `OP_MakeGroupLeaderMsg` | `0x0091f95f` serialize, `0x0091f977` deserialize | Single `u32` value. Name follows PacketParser v546 order; no v546 XML struct was found. |
| `77` | likely `OP_GroupCreatedMsg` | `0x0091f98f` serialize, `0x0091f9bc` deserialize | Two `u32` values. Name follows PacketParser v546 order; no v546 XML struct was found. |
| `78` | likely `OP_GroupDestroyedMsg` | `0x0091f9e9` serialize, `0x0091fa01` deserialize | Single `u32` value. Name follows PacketParser v546 order; no v546 XML struct was found. |
| `79` | likely `OP_GroupMemberAddedMsg` | `0x00928387` serialize, `0x0094a6fd` deserialize | Group tree: two leading `u32`, six one-byte group flags, `u8` direct member count, direct member entries of `u32`, `string16`, four `u32`; then `u8` subgroup count, subgroup headers, and nested member entries of the same member shape. |
| `80` | likely `OP_GroupMemberRemovedMsg` | `0x0091fa19` serialize, `0x0091fa46` deserialize; factory `0x0091b987`, type-info `0x00915b20` | Two `u32` values. |
| `81` | likely `OP_GroupRemovedFromGroupMsg` | `0x0091fa73` serialize, `0x0091fae7` deserialize | `u32`, `u32`, `string16`, then four `u32` values. |
| `82` | likely `OP_GroupLeaderChangedMsg` | `0x0091fb5b` serialize, `0x0091fb88` deserialize | Two `u32` values. Name follows PacketParser v546 order; no v546 XML struct was found. |
| `83` | likely `OP_GroupResendOOZDataMsg` | `0x0091fbb5` serialize, `0x0091fbe2` deserialize | Two `u32` values. Name follows PacketParser v546 order; no v546 XML struct was found. |
| `84` | likely `OP_GroupSettingsChangedMsg` | `0x0091fc0f` serialize, `0x0091fc3c` deserialize | Two `u32` values. Name follows PacketParser v546 order; no v546 XML struct was found. |
| `85` | likely `OP_OutOfZoneMemberDataMsg` | `0x0091fc69` serialize, `0x0091fc81` deserialize | Single `u32` value. PacketParser v546 names this row, but the legacy source oplist omits it; no v546 XML struct was found. |
| `86` | likely `OP_SendLatestRequestMsg` | `0x0091fc99` serialize, `0x0091fd30` deserialize | `u32` followed by six `u8` flag bytes. PacketParser v546 names this row, but the legacy source oplist omits it; no v546 XML struct was found. |
| `87` | likely `OP_ClearDataMsg` | `0x00926c2b` serialize, `0x00926c61` deserialize | `u32`, `u32`, then nested status block `0x00993b5c`/`0x00993bc9`: four `u32`, four `u8`, and `string16`. |
| `88` | `OP_SetSocialMsg` | `0x0091fdd8` serialize, `0x0091fe05` deserialize; factory `0x0091baf5`, type-info `0x0046aec0` | Exact v546 layout is `u8`, `u16`; this overrides the older social-list XML rows for DoF. |
| `89` | `OP_ClearDataMsg` | `0x0091fe32` serialize, `0x0091fe4a` deserialize; handler `0x0046c510` | Single `u8 data_type`; the handler asserts/logs if the byte is not `1`, then clears a 100-entry data/status table and resets cached status state. This is not the older source-order `OP_ESStatusMsg` label. |
| `90` | `OP_ESZoneInstanceStatusMsg` | `0x0091fe62` serialize, `0x0091fea9` deserialize; handler `0x00491e10` | `u8 status`, two `string16` values, and trailing `u16`. The handler stores the trailing `u16` in GameScene status state and forwards the status plus the two string fields to the instance-status UI/path. |
| `91` | likely `OP_ZonesStatusRequestMsg` | `0x0091fef0` serialize, `0x0091ffb2` deserialize; factory `0x0091bb32`, type-info `0x00915cda` | Two leading `u32` values, then `0x19b` interleaved `u32,u32` pairs copied from two fixed in-memory arrays (`object+0x0c` and `object+0x678`), followed by five trailing `u32` values. Name follows PacketParser/source order; no v546 XML struct was found. |
| `92` | likely `OP_ZonesStatusMsg` | `0x0092864e` serialize, `0x0094475d` deserialize | Packed `u16` zone count; each entry is `string16` plus four `u32` values. |
| `93` | likely `OP_ESWeatherRequestMsg` | `0x00920074` serialize, `0x0092008c` deserialize; factory `0x0091bb72`, type-info `0x00915d1b` | Single `u32`. |
| `94` | `OP_ZonesStatusMsg` | `0x0093692b` serialize, `0x00944820` deserialize; handler `0x00479d10` | Starts with `u32`, packed outer count, then nested zone-server entries with `string16`, two `u32` fields, packed inner count, and inner instance entries of `string16` plus three `u32` fields. The handler prints `EverQuest II Zone Servers and Instances` and totals server/instance/client/entity/actor counts, so this is not the older source-order `OP_ESWeatherRequestEndMsg` label. |
| `95` | likely `OP_DialogSelectMsg` | `0x00918f2d` serialize, `0x00918f56` deserialize | Two `string16` values. |
| `96` | likely `OP_DialogCloseMsg` | `0x00918f7f` serialize, `0x00918fa8` deserialize | Two `string16` values. |
| `97` | likely `OP_RemoveSpellEffectMsg` | `0x009286ee` serialize, `0x009462d8` deserialize | `u32`, packed `u16` count, then `count` repeated `string16` values. |
| `98` | remove-concentration / quest-journal adjacent scalar row | `0x009200a4` serialize, `0x009200d1` deserialize; factory `0x0091bbaf`, type-info `0x005829b0` | Two `u32` values. PacketParser has gaps around `102`-`103`; treat names in this area as provisional unless a handler confirms them. |
| `99` | likely `OP_QuestJournalOpenMsg` | `0x009200fe` serialize, `0x00920116` deserialize; factory `0x0091bbc5`, type-info `0x00582a00` | Single `u32 quest_id`. Name follows the source handler/order after `OP_RemoveConcentrationMsg`; the handler reads a leading quest id and calls `SendQuest(quest_id)`. The v546 root override table now maps this handled row directly, even though no XML struct is needed because the server reads the leading id manually. |
| `100` | likely `OP_QuestJournalInspectMsg` | `0x0092012e` serialize, `0x0092015b` deserialize; factory `0x0091bbdb`, type-info `0x00915d98` | `u32 quest_id`, then one extra `u32` not used by the legacy source handler. Name follows the source handler/order after `OP_QuestJournalOpenMsg`; field semantics still need handler evidence for the second value. |
| `101` | unresolved quest-journal gap scalar row | `0x00920188` serialize, `0x009201c4` deserialize; factory `0x0091bc18`, type-info `0x00915dcd` | Three `u32` values. Do not label this as the commented `OP_SkillSlotMapping`; the active spell-slot mapping packet is `OP_KnowledgeWindowSlotMappingMsg` at type `300` and has a `u16` counted `(u32 spell_id, u16 slot_id)` list. |
| `102` | unresolved quest-journal gap empty row | base empty serializer/deserializer | No custom payload recovered. PacketParser/source gaps leave the semantic name unknown. |
| `103` | unresolved quest-journal selection/focus row | `0x00920200` serialize, `0x00920258` deserialize; factory `0x0091bc92`, type-info `0x006078a0`; send sites `0x0061cdf0`, `0x0061cf20`, `0x0061d340`, `0x0061d890` | Serializer writes `u32`, `u32`, `u8`, `u8`, `u32`. Quest Journal UI send sites initialize the two ids and two flags when selecting or refreshing quest focus; the trailing `u32` is serialized but does not yet have stable recovered semantics. PacketParser/source gaps leave the exact opcode name unknown. |
| `104` | `VeQuestJournalSetVisibleMsg` | `0x009202b0` serialize, `0x00936ad1` deserialize | `u32 selection_count`, `selection_count` repeated `u32`, trailing `u8`; deserializer rejects counts `>= 0x401`. v546 XML now exposes the counted quest-id list, and the source handler applies the visible flag to each selected quest id. |
| `105` | likely `OP_QuestJournalWaypointMsg` | `0x0092030e` serialize, `0x00920326` deserialize; factory `0x0091bca8`, type-info `0x00607850` | Single `u32 quest_id`. Name follows the source handler/order immediately before `OP_CreateGuildRequestMsg`; for v546 the source handler toggles the tracked state for the quest id. |
| `106` | likely `OP_CreateGuildRequestMsg` | `0x0092033e` serialize, `0x009203a4` deserialize | `u16`, `u32`, `u32`, `string8`, `u32`, `u32`. |
| `107` | likely `OP_CreateGuildReplyMsg` | `0x0092b24e` serialize, `0x009449b5` deserialize | `u32` status/response followed by the shared `GuildUpdatePayload` helper. The helper starts with `guild_name`, `guild_motd`, `guild_id`, a two-byte status/flag field, `guild_level`, formed/member fields, two experience `u64`s, compact event-filter words, eight rank blocks of `string16`/`u64` plus four `u32`/`u64` permission pairs, and a counted `u32`/`string16` list. |
| `108` | likely `OP_GuildsayMsg` | `0x0092040a` serialize, `0x0092046e` deserialize | `u32`, `u32`, `string16`, two `u8`, `string16`. |
| `109` | likely `OP_GuildKickMsg` | `0x009204d2` serialize, `0x00920528` deserialize | `string16`, `string16`, `u32`, `u8`, `u16`. Name follows the commented `OP_GuildKickMsg` slot in `emu_oplist.h` between `OP_GuildsayMsg` and `OP_GuildUpdateMsg`; field semantics still need handler evidence. |
| `110` | `OP_GuildUpdateMsg` | `0x0092b275` serialize, `0x009449dc` deserialize; handler `0x004b1d50` | Thin wrapper around `GuildUpdatePayload` `0x0092a979`/`0x00944570`, a large guild update body with guild strings, id/status/guild-level, timestamp/member/experience fields, compact event-filter pairs, eight rank-permission blocks, and a counted id/string tail list. `WS_GuildUpdate` now has a v546-only row for this body; the older base row and 562+ rows are left intact for other clients. |
| `111` | unresolved guild-adjacent single-u32 row | `0x00924e79` serialize, `0x00924e91` deserialize; factory `0x0091ce81`, type-info `0x00917069` | Single `u32`. Do not label this as `OP_DeleteGuildMsg`; the stronger source-order `OP_DeleteGuildMsg` row is client type `322` (`0x00924def`/`0x00924e07`). |
| `112` | likely `OP_FellowshipExpMsg` | `0x0092057e` serialize, `0x009205ba` deserialize; factory `0x0091bcbe`, type-info `0x00915e97` | Three `u32` values. |
| `113` | unnamed guild/consignment-adjacent scalar row | `0x009205f6` serialize, `0x00920632` deserialize; factory `0x0091bcfb`, type-info `0x00915ee4` | `u32`, `u32`, `u8`. This is an extra client row between the likely `OP_FellowshipExpMsg` and `OP_ConsignmentCloseStoreMsg` slots; no emulator handler/struct evidence found yet. |
| `114` | likely `OP_ConsignmentCloseStoreMsg` | `0x0092066e` serialize, `0x00920697` deserialize | `u32` followed by shared consignment/store payload helper `0x00992bc2`/`0x00992edf`; helper has a compact alternate path and a larger path with several strings, fixed fields, and a size-prefixed blob. |
| `115` | likely `OP_ConsignItemRequestMsg` | `0x009206c0` serialize, `0x009206d8` deserialize | Single `u32` payload. Name follows PacketParser/source order after `OP_ConsignmentCloseStoreMsg`; no handler/struct evidence found yet. |
| `116` | likely `OP_ConsignItemResponseMsg` | `0x009206f0` serialize, `0x00920773` deserialize | Shared base fields: `u64`, four `u32`, `u64`, `u8`, `u64`. Name follows PacketParser/source order, but the same local serializer/deserializer is reused by later character-transfer rows, so semantics remain provisional. |
| `117` | `VePurchaseConsignmentResponseMsg` / likely `OP_PurchaseConsignmentResponseMsg` | `0x009207f6` serialize, `0x00936b63` deserialize | `u8`, several fixed `u64`/`u32`/`u8` fields, two `string16` values, `u32 blob_size`, raw blob bytes, and trailing 8 bytes; deserializer rejects blob sizes `>= 0x10001`. The client assertion string is stronger than the shifted/commented PacketParser/source-order candidates in this neighborhood. |
| `118` | likely `OP_HouseDeletedRemotelyMsg` | `0x0091902a` serialize, `0x00919040` deserialize | Single `string16`. |
| `119` | likely `OP_UpdateHouseDataMsg` | `0x00919056` serialize, `0x0091907f` deserialize | Two `string16` values. |
| `120` | likely `OP_UpdateHouseAccessDataMsg` | `0x00920a49` serialize, `0x00920a74` deserialize | `string16`, `u32`; v546 XML now has this compact body while the 562 row keeps the later four-field struct. |
| `121` | likely `OP_PlayerHouseBaseScreenMsg` | `0x00920a9f` serialize, `0x00920b06` deserialize | `string16`, `u32`, `u8`, `u16 blob_size`, raw blob bytes. v546 XML now has a compact row instead of inheriting the later full house-window body. |
| `122` | likely `OP_PlayerHousePurchaseScreenMsg` | `0x00920b6d` serialize, `0x00920be6` deserialize | `string16`, `u32`, `u32`, `u8 flag`; when flag is zero, an extra `string16` and `u8` follow. v546 XML now has a compact row instead of the later coin/status-heavy purchase body. |
| `123` | `OP_PlayerHouseAccessUpdateMsg` | `0x00928746` serialize, `0x00946da2` deserialize; handler `0x00473e50` | Large house access update with `u32`, `string16`, fixed `u64`/`u32` fields, four flag bytes, optional `u8`-counted entries of `u32`/`string16`/`u8`, counted access entries of `u32`/`string16`/`u64`/`u32`/`u64`/`u32`/`u32`, counted visitor entries of `u32`/`string16`/`u64`/`u32`/`u32`, and trailing `u8`, `u16`, `u16`, `u16`. Deserializer vector helpers now identify the optional/access/visitor element strides as `0x1c`, `0x38`, and `0x28`; v546 XML now has this body while a 562 row preserves the old four-field legacy struct for later clients. |
| `124` | `OP_PlayerHouseDisplayStatusMsg` | `0x00920d7d` serialize, `0x00920e07` deserialize; handler `0x00473e70` | `string16`, `u32`, `u64`, `u32`, `u64`, `u32`, `string16`, `u8`. |
| `125` | `OP_PlayerHouseCloseUIMsg` | `0x00920e95` serialize, `0x00920ef3` deserialize; handler `0x00473e90` | `u32`, `u8 flag`; when flag is zero, an extra `string16` and `u8` follow. |
| `126` | buy-house status/response row | `0x00920f55` serialize, `0x00920f75` deserialize; handler `0x00473eb0` | Single status/response byte routed to the active house purchase/player-house UI handlers. Do not map the server's active `OP_BuyPlayerHouseMsg` handler here; that handler expects a request `house_id`. |
| `127` | `OP_BuyPlayerHouseTintMsg` provisional | base empty serializer/deserializer; handler `0x00695c30` | No custom payload fields. The source-order name is plausible, but the handler only resolves the `PlayerHouse` UI and does not expose additional semantics. |
| `128` | `OP_BuyPlayerHouseMsg` | `0x00920f99` serialize, `0x00920fb1` deserialize; send site `0x0069b9cc` | Single `u32 house_id`. The PurchaseHouse Buy button path checks the selected house id at object `+0x124`, builds vtable `0x00d059e8`, writes the id, and sends this row. v546 root mapping now points the server's buy-house request handler here. |
| `129` | house-adjacent scalar row | `0x00920fc9` serialize, `0x00920ff6` deserialize | `u32`, `u8`. |
| `130`-`131` | house-adjacent scalar rows | `0x00921023`/`0x00921053` serialize | Each row is a single `u32`; exact opcode names still need handler or send-site evidence. |
| `132` | likely `OP_EnterHouseMsg` | `0x00921083` serialize, `0x0092109b` deserialize; send site `0x006e0510` | Single `u32 room_id`. The `PlayerHouse`/visit UI reads `ROOMID`, checks `ACCESS` and `UPKEEP_DUE`, then sends this row when the room can be entered directly. |
| `133` | likely `OP_ExitHouseMsg` | base empty serializer/deserializer; send site `0x00694340` | Empty payload. The PlayerHouse button handler sends this row from the in-house branch adjacent to the type `132` enter path. |
| `134` | likely `OP_HouseDefaultAccessSetMsg` | `0x009210b3` serialize, `0x009210e9` deserialize; send site `0x00694200` | `u32 house_id`, `u8 access_level`. The PlayerHouse default-access UI reads `AccessEnumValue` and sends this row. v546 XML now uses this compact body instead of the inherited 64-bit house id, with a 562 row preserving the legacy body. |
| `135` | `OP_HouseAccessSetMsg` outbound candidate | `0x00921120` serialize, `0x00921163` deserialize; send site `0x00694340` | `u32 house_id`, `string16 player_name`, `u8 access_level`. The PlayerHouse access UI logs `Sending access set message to server...`, reads `AccessEnumValue`, builds this row through `0x00693440`, and sends it. This does not match `WS_MoveableObjectPlacementCriteri`, whose layout is type `138`; v546 XML now has the compact access-set row, with a 562 row preserving the legacy body. |
| `136` | `OP_HouseAccessRemoveMsg` outbound candidate | `0x009211a7` serialize, `0x009211d4` deserialize; factory `0x0091bf70`, type-info `0x00690720`; send site `0x00694340` | `u32 house_id`, `u32 player_dbid`. The PlayerHouse access UI logs `Sending remove message to server...`, reads `PlayerDBID`, and sends this row. v546 XML now uses the two-`u32` body, with a 562 row preserving the legacy body. |
| `137` | likely `OP_PayHouseUpkeepMsg` | `0x00921201` serialize, `0x0092122e` deserialize; factory `0x0091bf86`, type-info `0x006907c0`; send site `0x00694340` | `u32 house_id`, `u8 payment_flag`. The send site is in the PlayerHouse UI payment path and derives the flag by comparing cached coin/status amounts before sending. v546 XML now omits the two legacy extra flag bytes. |
| `138` | `OP_MoveableObjectPlacementCriteria` | `0x0092125b` serialize, `0x00921297` deserialize; factory `0x0091bf9c`, type-info `0x0091618e`; handler `0x0046c590` | Three `u32` values, matching `WS_MoveableObjectPlacementCriteri`; GameScene copies them into placement state at `+0x38c..+0x394`. |
| `139` | `OP_EnterMoveObjectModeMsg` | `0x0092b286` serialize, `0x0092ce43` deserialize; handler `0x0046c5c0` | `u32 spawn_id`, `u8 placement_mode`, `u16 model_type`, two 4-byte float/raw fields, `u8 count`, then `count` repeated `u32` values. v546 XML now includes the counted `u32` tail. Do not label this as `OP_PositionMoveableObject`; final placement is the separate send-site-backed type `140`. |
| `140` | `OP_PositionMoveableObject` | `0x00928a06` serialize, `0x009212d3` deserialize; send site `0x00534870` | The `place_item` path sends vtable `0x00ce7194`, which is registry type `140`: `u32 spawn_id`, three coordinate floats, two derived angle floats computed from the placement vector, and trailing `u32`. v546 XML now uses this 28-byte body; the old 32-byte body is preserved from `ClientVersion="562"`. |
| `141` | `OP_CancelMoveObjectModeMsg` | `0x0092139e` serialize, `0x009213b6` deserialize; factory `0x0091bfe8`, type-info `0x0052b0b0`; send site `0x005349d0` | Single `u32 spawn_id`. The cancel path restores cached moved-object state at GameScene `+0x398`, sends this row when called with the acknowledgement flag, and frees the cached state. PacketParser labels type `139` as cancel and type `141` as replaceable submeshes, but the recovered DoF client send sites show cancel is type `141`. |
| `142` | `OP_HouseCustomizationScreenMsg` | `0x0092b322` serialize, `0x009449f5` deserialize; handler `0x004a6530` | `u8 count`, then `count` entries of `u32` plus `string16`; the handler applies changed entries against the GameScene customization tree at `+0xec`. |
| `143` | `OP_CustomizationPurchaseRequestMsg` | `0x0092b380` serialize, `0x00944a87` deserialize; handler `0x004ab450` | Packed count, `count` entries of `u32` plus `string16`, then trailing `u8`; the handler stores the trailing flag at GameScene `+0xf8`. |
| `144` | `OP_CustomizationSetRequestMsg` | `0x00928b5f` serialize, `0x0094707f` deserialize; handler `0x00473ef0` | `string16`, `u32`, packed count, then entries of `u32`, `string16`, `u64`, `u32`, `u64`, `u32`, `u8`, `u8`; the handler routes it to the `InteriorCustomization` UI. |
| `145`-`146` | unresolved shader/tint selection rows | `0x009213ce`/`0x00921428` serialize; send sites `0x00602bc0`/`0x00602b00` | Each row is two `u32` values. Both UI send sites read a `ShaderID` property and send a customization/house object id plus the shader id through separate UI paths. PacketParser labels this area as customization reply/tint widgets, but exact opcode names remain unresolved because the assertion-backed reply/consignment rows are shifted later. |
| `147` | `OP_CustomizationReplyMsg` | `0x00921482` serialize, `0x009214c7` deserialize | Handler-backed at `0x00473f10`: `u32`, `u32`, `u8`, routed to `Eq2GuiInteriorCustomizationWindow::update`. The last byte is widened to the reply code read at object `+0x0c`. |
| `148` | likely `OP_TintWidgetsMsg` | `0x00928c4b` serialize, `0x0093a4b9` deserialize | `u8 count`, then `count` tint entries of `u32 object_id`, `u8 red`, `u8 green`, `u8 blue`. v546 XML now includes the leading count and repeated entries. Do not name this as the consignment response; the assertion-backed response is actual type `150`. |
| `149` | likely `OP_ExamineConsignmentRequestMsg` | `0x00920927` serialize, `0x0092097f` deserialize; factory `0x0091bda7`, type-info `0x00915fa6` | `u32`, `u64`, `u32`, `u8`, `u8`. This fits the consignment examine request slot immediately before the assertion-backed `VeExamineConsignmentResponseMsg` at type `150`; individual field semantics still need handler or capture evidence. |
| `150` | `VeExamineConsignmentResponseMsg` / likely `OP_ExamineConsignmentResponseMsg` | `0x009209d7` serialize, `0x00936c86` deserialize | `u32`, `u64`, `u8`, `u8`, `u32 blob_size`, raw blob bytes. Deserializer rejects blob sizes `>= 0x10001`. |
| `151` | `OP_UISettingsResponseMsg` | `0x00920c63` serialize, `0x00920cc9` deserialize | Handler-backed at `0x0046c390`: `u16 byte_count` capped at `0x2000`, raw bytes, and trailing `u16`. The handler attaches the raw bytes to a `VeMemoryStream` and passes them through `0x006aa520`, which reads up to `0x2000` bytes plus a trailing `u16` into UI/settings state. The retained builder-side format string `VeUISettingsResponseMsg   buf: %d  settings: %d` is xref'd from `0x006a9871` in `FUN_006a9780`, confirming the response name. Do not treat this as PacketParser/source-order `UIReset`. |
| `152` | likely `OP_UIResetMsg` | base empty serializer/deserializer at `0x00917862`/`0x00917867` | Empty payload. Name follows the now-bounded source order between `OP_UISettingsResponseMsg` and the capture-backed `OP_KeymapLoadMsg`; GameScene routes this row to the explicit unhandled-message branch. |
| `153` | `OP_KeymapLoadMsg` | base empty serializer/deserializer at `0x0091786c`/`0x00917871` | Empty payload. Runtime capture sends this row during login, and EQ2Emu `LoginServer::Client` handles the same slot as `OP_KeymapLoadMsg`. |
| `154` | `OP_KeymapNoneMsg` | base empty serializer/deserializer at `0x00917876`/`0x0091787b` | Handler-backed through generic keymap handler `0x004fd080` for type `0x9a`; clears/defaults keymap state without reading payload bytes. v546 XML now has an empty row instead of the inherited data-bearing placeholder. |
| `155` | `VeKeymapDataMsg` / likely `OP_KeymapDataMsg` | `0x0092150e` serialize, `0x00921544` deserialize | `u32 packed_size`, then raw keymap bytes. Deserializer rejects sizes above `0x8000`. v546 XML now represents this as a size-prefixed byte array. |
| `156` | `VeKeymapSaveMsg` / likely `OP_KeymapSaveMsg` | `0x009215ac` serialize, `0x009215e2` deserialize | `u32 packed_size`, then raw keymap bytes. Deserializer rejects sizes above `0x8000`. |
| `157` | `VeDispatchSpellCmdMsg` / likely `OP_DispatchSpellCmdMsg` | `0x0092164a` serialize, `0x009216b8` deserialize | `u8 dispatch_method`, two `u32`; method `0` adds `u16 payload_size` and raw payload bytes. Deserializer rejects method-0 payload sizes above `0x8000`. |
| `158` | no recovered client registry row | n/a | The extracted registry skips type `158`. PacketParser places entity-verb rows around this numeric area, but the concrete request/reply/verb layouts are types `159`-`161`. |
| `159` | likely `OP_EntityVerbsRequestMsg` | `0x00921755` serialize, `0x0092176d` deserialize | Single `u32 spawn_id`; matches PacketParser `WS_EntityVerbsRequest`. |
| `160` | `OP_EntityVerbsReplyMsg` | `0x00928cd3` serialize, `0x00944b2d` deserialize; entry helper `0x0091aae4`/`0x0091ab4b`; handler `0x004aa7d0` | `u32 spawn_id`, `u8 verb_count`, then verb entries. Each entry is `string16 command`, 4-byte float/value, `u8 display_error`, `u8 flag`, optional `string16 error`, and `string16 display_text`. The handler rebuilds the `RadialMenu` verb list for the matching spawn. v546 XML now splits the legacy `int16 display_error` into the two client bytes. |
| `161` | likely `OP_EntityVerbsVerbMsg` | `0x00921785` serialize, `0x009217b0` deserialize | `u32 spawn_id`, `string16 command`. |
| `162` | `VeChatRelationshipUpdateMsg` / `OP_ChatRelationshipUpdateMsg` | `0x00928d38` serialize, `0x00944c3e` deserialize; handler `0x004adcb0` | `u32 account_id`, `u8 type`, `u32 count`, then `count` pairs of `string16 name` and `string16 location`. Deserializer rejects counts `>= 0x10001`; the client does not emit the PacketParser trailing `int16` per entry. v546 XML now omits that legacy per-entry tail. |
| `163` | `OP_LootItemsRequestMsg` | `0x0092b3eb` serialize, `0x0092ceeb` deserialize | Matches v546 `WS_LootItem`: `u32 loot_id`, `u8 loot_all`; when `loot_all == 0`, `u8 item_count` plus `item_count` repeated `u32 item_id`; then trailing `u32 target_id`. |
| `164` | `OP_StoppedLootingMsg` | `0x00921c31` serialize, `0x00921c49` deserialize; handler `0x00472d30` | Single `u32 spawn_id`; matches PacketParser `WS_StoppedLooting`. The handler clears/hides the active loot presentation for that spawn. |
| `165` | `OP_SitMsg` | `0x00917880` serialize, `0x00917885` deserialize; handler `0x00472fe0` | Stub serializer/deserializer; no custom payload fields. The handler sets the local player seated flag and updates sit/stand action availability. |
| `166` | `OP_StandMsg` | `0x0091788a` serialize, `0x0091788f` deserialize; handler `0x00473040` | Stub serializer/deserializer; no custom payload fields. The handler clears the local player seated flag and updates sit/stand action availability. |
| `167` | likely `OP_SatMsg` | `0x009178bc` serialize, `0x009178c1` deserialize | Stub serializer/deserializer; no custom payload fields. Name follows PacketParser v546 order and matches the empty legacy `WS_SatMsg` struct. |
| `168` | likely `OP_StoodMsg` | `0x009178c6` serialize, `0x009178cb` deserialize | Stub serializer/deserializer; no custom payload fields. Name follows PacketParser v546 order immediately after `OP_SatMsg`. |
| `169` | `OP_ClearForTakeOffMsg` | `0x0091789e` serialize, `0x009178a3` deserialize; handler `0x004c5fb0` | Stub serializer/deserializer; no custom payload fields. The handler resets the takeoff/flight state timer to `-1.0`. |
| `170` | `OP_ReadyForTakeOffMsg` | `0x00917894` serialize, `0x00917899` deserialize; handler `0x004c5f90` | Stub serializer/deserializer; no custom payload fields. The handler arms the takeoff/flight state timer when it is negative. |
| `171` | likely `OP_ShowIllusionsMsg` | `0x009178a8` serialize, `0x009178ad` deserialize | Stub serializer/deserializer; no custom payload fields. Name follows PacketParser v546 order after the takeoff toggles; no v546 XML struct was found. |
| `172` | likely `OP_HideIllusionsMsg` | `0x009178b2` serialize, `0x009178b7` deserialize | Stub serializer/deserializer; no custom payload fields. Name follows PacketParser v546 order after `OP_ShowIllusionsMsg`; no v546 XML struct was found. |
| `173` | likely `OP_ExamineItemRequestMsg` | `0x00921c61` serialize, `0x00921c9d` deserialize | Three `u32` values. This follows the consistent +1 shift from PacketParser v546 after `OP_StoppedLootingMsg`. |
| `174` | likely `OP_ReadBookPageMsg` | `0x00921cd9` serialize, `0x00921d16` deserialize | Two `u8` values stored as booleans by the client; exact field meanings unresolved. |
| `175` | likely `OP_DefaultGroupOptionsRequestMsg` | base empty serializer/deserializer | Empty payload matching `WS_DefaultGroupOptionsRequestMsg`. |
| `176` | `OP_DefaultGroupOptionsMsg` | `0x00921d5b` serialize, `0x00921de5` deserialize; handler `0x004fd080` | Six `u8` values matching `WS_DefaultGroupOptions` v546: `loot_method`, `loot_items_rarity`, `auto_split_coin`, `default_yell_method`, `default_group_lock_method`, `group_autolock`. The generic UI/options handler handles this row as type `0xb0`. |
| `177` | likely `OP_GroupOptionsMsg` | `0x00921e7e` serialize, `0x00921f08` deserialize | Six `u8` values in the same order as type `176`; exact client-side field meanings are inferred from adjacency. |
| `178` | `OP_DisplayGroupOptionsScreenMsg` | `0x00921fa1` serialize, `0x0092203e` deserialize; factory `0x0091c39a`, type-info `0x00916536`; handler `0x00472d60` | Seven `u8` values. The handler resolves `GroupOptions` and updates the visible option controls. The following two assertion-backed rows show that `DisplayInnVisit`/`DumpScheduler` are actual types `179`/`180`, one higher than PacketParser v546. |
| `179` | `VeDisplayInnVisitScreenMsg` / `OP_DisplayInnVisitScreenMsg` | `0x00928dc0` serialize, `0x00947179` deserialize; handler `0x00472d80` | `u32 count`, then entries of `u32 house_id`, `string16 owner`, `u32`, `u8`. Deserializer rejects counts `>= 0x10001`. The handler resolves `VisitInnRoom`, loads the list, and refreshes the UI; v546 XML now uses this recovered field order instead of the inherited `u32,string16,u8,u32` order, with a 562 row preserving the previous layout for later clients. |
| `180` | `VeDumpSchedulerMsg` / likely `OP_DumpSchedulerMsg` | `0x00928e4a` serialize, `0x0094722f` deserialize | `u32 count`, then entries of `u32`, `u32`, `string16`, `string16`, `u32`, `u32`, followed by trailing `u8`. Deserializer rejects counts `>= 0x10001`. |
| `181` | likely `OP_LSRequestPlayerDescMsg` | `0x009220ef` serialize, `0x0092211c` deserialize | Two `u32` values. Names in this small area remain provisional because nearby rows are shifted from PacketParser v546. |
| `182` | likely `OP_LSCheckAcctLockMsg` | `0x00922149` serialize, `0x00922161` deserialize | Single `u32`. |
| `183` | likely `OP_WSAcctLockStatusMsg` | `0x00922179` serialize, `0x009221a6` deserialize | `u32`, `u8`. |
| `184` | likely `OP_RequestHelpRepathMsg` | `0x009221d3` serialize, `0x009221eb` deserialize | Single `u32`. |
| `185` | likely `OP_RequestTargetLocMsg` | `0x009178da` serialize, `0x009178df` deserialize | Stub functions return true; no custom payload fields. |
| `186` | likely `OP_UpdateMotdMsg` | `0x009190a8` serialize, `0x009190be` deserialize | Single `string16`. This appears to be the custom payload row that PacketParser places at `185`. |
| `187` | `OP_PerformPlayerKnockbackMsg` | `0x00922203` serialize, `0x0092225d` deserialize; handler `0x00473080` | Five float-sized values matching the first five legacy `WS_PlayerKnockback` fields: `target_x`, `target_y`, `target_z`, `vertical_movement`, and `horizontal_movement`. This row appears one higher than PacketParser's v546 opcode number. The DoF client body omits the legacy XML trailing `unknown`, `use_player_heading`, and `unknown2[4]` byte fields. The handler applies the derived knockback vector to the local player. |
| `188` | `OP_PerformCameraShakeMsg` | `0x009222b7` serialize, `0x009222cf` deserialize; factory `0x0091c53d`, type-info `0x00916687`; handler `0x0046c460` | Single 4-byte `float intensity`, matching `WS_PerformCameraShakeMsg` v546 and the `PerformCameraShake` Lua sender. |
| `189` | `VePopulateSkillMapsMsg` / `OP_PopulateSkillMapsMsg` | `0x00936d2f` serialize, `0x00944d46` deserialize; handler `0x004ae180` | `u32 count`, then entries of `skill_id`, `short_name`, and resolved display `name` as two `string16` fields. Deserializer rejects counts `>= 0x10001`. The handler rebuilds client skill-map caches. v546 XML now uses `short_name`/`name` instead of inheriting the older `name`/`description` pair. |
| `190` | likely `OP_CancelledFeignMsg` | base empty serializer/deserializer | No custom payload recovered. |
| `191` | likely `OP_SignalMsg` | `0x009190d4` serialize, `0x009190ea` deserialize | Single `string16`. |
| `192` | `OP_ShowCreateFromRecipeUIMsg` | `0x0092b46d` serialize, `0x00949a67` deserialize; handler `0x00473280` | Large crafting UI payload: leading strings/fixed fields, counted `0x1c` entry lists, nested category records, and `u32`/`u8` pair lists. The packet-log decoder now maps the recovered wire layout. |
| `193` | `OP_CancelCreateFromRecipeMsg` | base empty serializer/deserializer; handler `0x004732a0` | No custom payload; closes/hides the active create-from-recipe flow in the `TradeSkills` UI. |
| `194` | likely `OP_BeginItemCreationMsg` | `0x0092b843` serialize, `0x009402b9` deserialize | Two `u32`, `u8` outer count, outer records with `u32` plus counted `u32`/`u8` pairs, then a trailing counted `u32`/`u8` pair list. |
| `195` | `OP_StopItemCreationMsg` | base empty serializer/deserializer; handler `0x00473400` | No custom payload; reenables trade-skill controls after an item creation session stops. |
| `196` | `OP_ShowItemCreationProcessUIMsg` | `0x009222e7` serialize, `0x009223e5` deserialize; handler `0x004732c0` | Four `u32`, two `u8`, then five repeated slot-state blocks with string/fixed fields and item-detail helper payloads. The packet-log decoder now maps the helper discriminator variants used by these slot records. |
| `197` | `OP_UpdateItemCreationProcessUIMsg` | `0x009224e3` serialize, `0x00922582` deserialize; handler `0x004732e0` | `u8`, two `u32`, `u8`, `u16`, `string16`, then three `u32` values. The handler uses the last two `u32` values as signed progress/durability deltas for the active combine target. |

Item-detail helper note: dispatchers `0x009704dd`/`0x00970517` write/read a
`u32` discriminator and use the pointer table initialized by `0x004b36e0` and
`0x00542e80` at object offsets `+0x498..+0x4a4`. Discriminator `0` targets the
complex EqCmd-widget-style body at subobject `+0x8`
(`0x009709fc`/`0x00973088`, with subtype cases `1..10`); discriminator `1`
targets subobject `+0x1f8` (`0x00970db1`/`0x0097346b`); discriminator `2`
targets subobject `+0x23c` (`0x00970e48`/`0x009711e5`); discriminator `3`
targets subobject `+0x3f0` (`0x009705e4`/`0x00972c1d`). Values outside `0..3`
carry no variant payload and the deserializer normalizes them to `-1`.

Legacy XML action: `server/WorldStructs.xml` now has a `ClientVersion="546"`
`WS_PlayerKnockback` override with only the five DoF client fields. A
`ClientVersion="562"` copy preserves the older trailing-byte layout for later
clients until that branch is separately verified.
| `198` | `OP_DisplayTSEventReactionMsg` | `0x00922623` serialize, `0x00922643` deserialize; factory `0x0091c5bc`, type-info `0x00916728`; handler `0x00473420` | Single `u8` routed to the tradeskill event/reaction display helper. |
| `199` | `VeShowRecipeBookMsg` / `OP_ShowRecipeBookMsg` | `0x0092b962` serialize, `0x0092fab2` deserialize; handler `0x00473450` | Fixed `0x29`-byte header, `u32 bit_count`, then `ceil(bit_count / 32)` `u32` mask words. Deserializer rejects bit counts `>= 0x10001`; v546 XML now sends a 40-byte device field plus the existing one-byte flag as the 0x29-byte header, followed by the current one-word mask source path. |
| `200` | likely `OP_KnowledgebaseRequestMsg` | `0x0092266a` serialize, `0x009226a2` deserialize | `u32`, `string16`, `string16`. |
| `201` | `VeKnowledgebaseResponseMsg` / `OP_KnowledgebaseResponseMsg` | `0x00928f02` serialize, `0x009434fa` deserialize; handler `0x00473470` | `u32`, `u32 count`, entries of `string16`, `u32`, `string16`, then trailing `string16`. Deserializer rejects counts `>= 0x10001`. The handler forwards the payload to the `Help` UI. |
| `202` | likely `OP_CSTicketHeaderRequestMsg` | `0x009226da` serialize, `0x009226f2` deserialize; factory `0x0091c5f9`, type-info `0x005b9420` | Single `u32`. |
| `203` | `VeCSTicketInfoMsg` / `OP_CSTicketInfoMsg` | `0x00928f97` serialize, `0x00944e69` deserialize; handler `0x00473490` | `u32`, `u32 count`, entries of three `u32`, `string16`, three `u32`, `string16`. Deserializer rejects counts `>= 0x401`. The handler forwards the ticket list to the `Help` UI. |
| `204` | likely `OP_CSTicketCommentRequestMsg` | `0x0092270a` serialize, `0x00922737` deserialize; factory `0x0091c60f`, type-info `0x005b9380` | Two `u32` values. |
| `205` | `VeCSTicketCommentResponseMsg` / `OP_CSTicketCommentResponseMsg` | `0x00922764` serialize, `0x00947319` deserialize; handler `0x004734b0` | Two `u32`, `u32 count`, entries of `u8`, `u32`, `string16`, `string16`. Deserializer rejects counts `>= 0x81`. The handler forwards the comment list to the `Help` UI. |
| `206` | likely `OP_CSTicketCreateMsg` | `0x00922805` serialize, `0x00922868` deserialize | Three `u32` values followed by three `string16` values. |
| `207` | likely `OP_CSTicketAddCommentMsg` | `0x009228cb` serialize, `0x00922905` deserialize | Two `u32` values followed by `string16`. |
| `208` | likely `OP_CSTicketDeleteMsg` | `0x0092293f` serialize, `0x0092296c` deserialize | Two `u32` values. |
| `209` | `OP_CSTicketChangeNotificationMsg` | `0x00922999` serialize, `0x009229de` deserialize; handler `0x004734d0` | `u32`, `u8`, `u32`. The handler forwards the compact notification to the `Help` UI. |
| `210` | likely `OP_WorldDataUpdateMsg` | `0x00922a24` serialize, `0x00922a96` deserialize | `string16`, raw 8-byte field, `u16 blob_size`, raw blob bytes, `string16`, trailing `u8`. |
| `211` | `OP_KnownLanguagesMsg` | `0x00922b92` serialize, `0x0092fb8a` deserialize; handler `0x00485090` | `u8 count`, `count` repeated `u8` language ids, trailing `u8`. The handler adds languages when the trailing flag is clear and removes them from the client language set when it is set; v546 XML now omits the later `current_language` byte and a 562 row preserves the legacy body. |
| `212` | likely client crash-log request gap | no registry row recovered | PacketParser has `OP_LsRequestClientCrashLogMsg`; direct static extraction skipped from type `211` to `213`. |
| `213`-`217` | LS client log reply constructed-message rows | compressed-log functions already recovered | The static registry rows are base-empty, but constructed message functions use the shared compressed log blob body documented below. |
| `218` | likely `OP_ClientTeleportToLocationMsg` | `0x00922cfb` serialize, `0x00922d5e` deserialize | Three `string16` values, raw `0x0c` coordinate block, and two trailing `u8` flags. |
| `219` | `OP_UpdateClientPredFlagsMsg` | `0x00922dc1` serialize, `0x00922df7` deserialize; handler `0x0046c5f0` | `u32`, `u8`. The handler uses the trailing flag to toggle local player/client prediction state. |
| `220` | `OP_ChangeServerControlFlagMsg` | `0x00922e31` serialize, `0x00922e51` deserialize; handler `0x0046c620` | Single `u8 toggle`. The handler updates the local player's server-control flag and resets movement/control state when the flag clears; this row is not the CSTools request despite the nearby source-order label. |
| `221` | likely `OP_CSToolsResponseMsg` | `0x00922e78` serialize, `0x00922edd` deserialize | Two `string16` values followed by four `u32` values. |
| `222` | likely `OP_AddSocialStructureStandingMsg` | `0x00922f42` serialize, `0x00922f8b` deserialize | `string16` followed by three `u32` values. |
| `223` | unresolved boat/social-standing-adjacent row | `0x00922fd4` serialize, `0x0092302c` deserialize | `string16`, `u32`, `u8`, `u32`, `u32`. Exact opcode label still needs a handler tie-in. |
| `224` | likely `OP_CreateBoatTransportsMsg` | `0x0092908f` serialize, `0x00944f95` deserialize | `u8 count`, then entries of `u32`, three `string16` values, and `u16`. `server/WorldStructs.xml` now has a `ClientVersion="546"` counted-list row. The current automount call site sends an empty list explicitly, avoiding the legacy one-byte `path_id = 1` packet that looked like a count of one but had no entry body. |
| `225` | likely `OP_PositionBoatTransportMsg` | `0x00923084` serialize, `0x009230bd` deserialize | `u32`, `u16`. |
| `226` | likely `OP_MigrateBoatTransportMsg` | `0x009230f4` serialize, `0x0092310c` deserialize | Single `u32`. |
| `227` | likely `OP_MigrateBoatTransportReplyMsg` | `0x00923124` serialize, `0x0092315a` deserialize | `u32`, `u8`. |
| `228` | likely `OP_DisplayDebugNLLPointsMsg` | `0x00929145` serialize, `0x0093a546` deserialize | `string16`, then three `u16`-count lists; each list entry is six `u32` values. |
| `229` | `VeExamineInfoRequestMsg` / `OP_ExamineInfoRequestMsg` | `0x00923194` serialize, `0x0092323c` deserialize; factory `0x0091c888`, type-info `0x0045a450`; handler `0x00461380` | `u8 request_type`, then conditional payload: type `1` uses `u64`; types `2`/`3` use an extra `u32` plus a primary `u32`; types `0`/`4`/`5` use a primary `u32`; all valid paths end with two `u8` flags. Unsupported types assert. The handler switches on the same request type and routes to ExamineManager helpers. |
| `230` | `VeQuickbarInitMsg` / `OP_QuickbarInitMsg` | `0x0092ba03` serialize, `0x0094505e` deserialize; entry helper `0x0091a7df`/`0x0091a875` | Handler-backed at `0x004a3180`: `u32 count`, then quickbar entries. Serializer entry helper writes eight `u32` values and two `string16` values; deserializer rejects counts `>= 0x401`. The handler iterates entries and installs hotkeys through `0x0049cfe0`. |
| `231` | likely `OP_QuickbarUpdateMsg` | `0x009232e4` serialize, `0x009232f5` deserialize | Thin wrapper around the quickbar entry helper. |
| `232` | `VeMacroInitMsg` / `OP_MacroInitMsg` | `0x0092ba57` serialize, `0x00948dd0` deserialize; entry helper `0x00927b3a`/`0x00945dae` | Handler-backed at `0x004b0270`: `u32 count`, then macro entries. Macro entry helper writes `u8`, a one-byte-length string field, up to three `string16` command strings, and trailing `u16`; deserializer rejects counts `>= 0x401`. The handler logs `m_processMacroInitMsg` and installs entries through `0x004ae9f0`. |
| `233` | likely `OP_MacroUpdateMsg` | `0x0092935a` serialize, `0x0094633d` deserialize | Thin wrapper around the macro entry helper. |
| `234` | likely `OP_QuestionnaireMsg` | `0x00923308` serialize, `0x009233f9` deserialize | `u32`, `u8 has_body`; when set, `u32`, nine `string16` values, and three trailing `u8` values. |
| `235` | `OP_LevelChangedMsg` | `0x009234ea` serialize, `0x00923526` deserialize; factory `0x0091c89e`, type-info `0x009169eb`; handler `0x0049c660` | Matches `WS_LevelChanged`: `u16 old_level`, `u16 new_level`, `u8 type`. The handler displays adventure/tradeskill level-up or level-down text; the adjacent `OP_SpellGainedMsg` v546 struct is larger and string-bearing, so it does not fit this row. |
| `236` | `OP_DisplayWarningMsg` | `0x00923562` serialize, `0x009235fd` deserialize; handler `0x00474e50` | `u8`, two `u32`, `string16`, three `u8`, two `u16`, and trailing `u32`. The handler routes the payload to `0x00687520`; do not label this row as `OP_EncounterBrokenMsg`. |
| `237` | `OP_EncounterBrokenMsg` | `0x0091926e` serialize, `0x00919284` deserialize; handler `0x0049c990` | Single `string16`. The handler formats the retained "Encounter Broken!" UI string and triggers the encounter-broken visual path. `server/WorldStructs.xml` now has a v546 override that omits the legacy unused trailing bytes. |
| `238` | `OP_OnscreenMsgMsg` | `0x00923698` serialize, `0x00923708` deserialize; handler `0x00474ec0` | `u8`, two `string16` values, `u32` float-sized field, and three trailing `u8` color values. This matches `WS_OnScreenMsg` v546 and the source `Client::SendPopupMessage` path. |
| `239` | `OP_ModifyGuildMsg` | `0x00923778` serialize, `0x009237df` deserialize; handler `0x00486c60` | Three one-byte-length string fields; one serializer path adds three trailing `u8` values when an internal list is empty. The GameScene handler derives a command/value triplet from either the optional vector or fallback bytes before posting the modify-guild action. EQ2Emu's numeric `WS_ModifyGuild` status-delta struct does not match this body and is now 562+ only. |
| `240` | likely `OP_GuildEventMsg` | `0x00923846` serialize, `0x009238ba` deserialize | Five `u32` values followed by two `string16` values. |
| `241` | unresolved guild-event-adjacent row | `0x0092392e` serialize, `0x00923977` deserialize | `u32`, `u32`, `string16`, `u32`. This does not match the legacy XML `WS_GuildEventAdd` shape, so the exact opcode label remains unresolved. |
| `242` | `OP_GuildEventAddMsg` | `0x009239c0` serialize, `0x00923a18` deserialize; handler `0x00472e00` | Matches `WS_GuildEventAdd`: `u32 account_id`, `u64 event_id`, `u32 type`, `u32 date`, `string16 description`. |
| `243` | `OP_GuildEventActionMsg` | `0x00923a70` serialize, `0x00923aac` deserialize; factory `0x0091c8db`, type-info `0x00916a74`; handler `0x00472de0` | Matches `WS_GuildEventAction` when the `u8 action` plus three unknown bytes are packed as one trailing `u32`. The concrete guild-event list payload is actual type `244`. |
| `244` | `OP_GuildEventListMsg` | `0x0092936d` serialize, `0x00936e37` deserialize; handler `0x00472da0` | `u32`, `u16 count`, `count` `u64` ids, then `count` `u8` flags. Deserializer rejects counts `>= 0x1f5`. |
| `245` | likely `OP_GuildEventDetailsMsg` | `0x00929404` serialize, `0x00936ee0` deserialize | Matches `WS_GuildEventDetails`: `u32`, `u16 count`, then `count` `u64` ids. Deserializer rejects counts `>= 0x1f5`; emulator send path now sets `num_events` before writing the id array. |
| `246` | `OP_RequestGuildInfoMsg` | `0x00923ae8` serialize, `0x00923b49` deserialize; handler `0x00472dc0` | Matches `WS_RequestGuildInfo`: `u32 account_id`, `u64 event_id`, `u32 date`, `u32 type`, `string16 description`. |
| `247` | unresolved guild-info-adjacent row | `0x00923baa` serialize, `0x00923bf3` deserialize | Three `u32` values followed by `string16`; exact opcode label remains unresolved. |
| `248` | `OP_GuildBankActionMsg` | `0x00923c3c` serialize, `0x0093704d` deserialize; handler `0x00472e40` | Complex guild-bank action with fixed fields, raw `0x0c`, two one-byte-length string fields, `u16 blob_size` plus blob, `u64`, and trailing `u8`. The handler currently only branches on action/status at object `+0x08` for UI refresh and tab-cache clearing. |
| `249` | likely `OP_GuildBankActionResponseMsg` | `0x00923d4f` serialize, `0x00937163` deserialize | Complex guild-bank response with fixed fields, raw `0x0c`, `u64`, `u8`, `u16 blob_size`, and raw blob. |
| `250` | likely `OP_GuildBankItemDetailsResponseMsg` | `0x00923e22` serialize, `0x0093723a` deserialize | `u32`, `u8`, two `u32`, `u16 blob_size`, raw blob. |
| `251` | guild-bank small update shape | `0x00923ea5` serialize, `0x00923f09` deserialize | `u32`, `u8`, `u32`, `u8`, raw `0x0c`, one-byte-length string field. The separate constructed `VeGuildBankUpdateMsg` at type `252` still uses the older size-prefixed blob envelope. |
| `252` | `OP_GuildBankUpdateMsg` / `VeGuildBankUpdateMsg` | `0x00923f6d` serialize, `0x00923fc2` deserialize; handler `0x00472ea0` | `u32`, `u8`, `u32 size`, raw blob; deserializer rejects sizes `>= 0x1001`. The handler treats the `u8` as a bank/tab index and decodes the blob into a `0x2d8`-byte cached tab snapshot. EQ2Emu's v546 guild-login empty-tab packets now use this 9-byte zero-blob envelope instead of the old 8-byte `u32,u32` placeholder. |
| `253` | `OP_GuildBankEventListMsg` | `0x0092946f` serialize, `0x00936f51` deserialize; handler `0x00472e20` | `u32`, `u8`, `u16 count`, then `count` `u64` ids. Emulator send path now uses a distinct per-event array index instead of reusing the bank number. |
| `254` | likely guild-bank event details/request row | `0x009294e8` serialize, `0x00936fcf` deserialize | Same envelope as type `253`: `u32`, `u8`, `u16 count`, then `count` `u64` ids. |
| `255` | `OP_RewardPackMsg` | `0x009296e1` serialize, `0x0094ad29` deserialize; helper `0x00929561`/`0x0094ab94`; handler `0x00474f40` | Reward pack helper writes fixed reward fields, two counted item-detail lists, and a counted `string16`/`u32` list; deserializer rejects item-list counts above `0x40`. Each item-list entry is `u32`, `u16`, then the shared item-detail helper payload described above. The handler reads the status byte at message `+0x08` and can route negative-status payloads through the `RewardPack` UI. |
| `256` | likely `OP_RenameGuildMsg` | `0x00924047` serialize, `0x00924072` deserialize | `u32`, `string16`. |
| `257` | likely `OP_ZoneToFriendRequestMsg` | `0x0092409d` serialize, `0x009240c8` deserialize | `string16`, `u32`. |
| `258` | likely `OP_ZoneToFriendReplyMsg` | `0x009240f3` serialize, `0x0092411e` deserialize | `string16`, `u32`. |
| `259` | likely `OP_ChatCreateChannelMsg` | `0x009217db` serialize, `0x00921822` deserialize | `u8`, `u32`, two `string16` values. |
| `260` | likely `OP_ChatJoinChannelMsg` | `0x00921869` serialize, `0x009218a1` deserialize | `u32`, two `string16` values. |
| `261` | likely `OP_ChatWhoChannelMsg` | `0x009218d9` serialize, `0x00921904` deserialize | `u32`, `string16`. |
| `262` | likely `OP_ChatLeaveChannelMsg` | `0x0092192f` serialize, `0x0092195a` deserialize | `u32`, `string16`. |
| `263` | likely `OP_ChatTellChannelMsg` | `0x00921985` serialize, `0x009219bd` deserialize | `u32`, two `string16` values. |
| `264` | likely `OP_ChatTellUserMsg` | `0x009219f5` serialize, `0x00921a2d` deserialize | `u32`, two `string16` values. |
| `265` | likely `OP_ChatToggleFriendMsg` | `0x00921a65` serialize, `0x00921a9f` deserialize | `u8`, `u32`, `string16`. |
| `266` | likely `OP_ChatToggleIgnoreMsg` | `0x00921ad9` serialize, `0x00921b13` deserialize | `u8`, `u32`, `string16`. |
| `267` | likely `OP_ChatSendFriendsMsg` | `0x00921b4d` serialize, `0x00921b65` deserialize | Single `u32`. |
| `268` | likely `OP_ChatSendIgnoresMsg` | `0x00921b7d` serialize, `0x00921b95` deserialize | Single `u32`. |
| `269` | likely `OP_ChatFiltersMsg` | `0x00921bad` serialize, `0x00921bf5` deserialize; factory `0x0091c1d2`, type-info `0x00635f60` | `u16 byte_count` followed by raw bytes; serializer clamps count to `0x32`. Name follows the source-order slot immediately after `OP_ChatSendIgnoresMsg` and before mail. `server/WorldStructs.xml` now has a `ClientVersion="546"` override for this bounded raw filter blob instead of the legacy fixed sequence of eight `u16` fields. |
| `270` | likely `OP_MailGetHeadersMsg` | `0x00925061` serialize, `0x00925079` deserialize; factory `0x0091cfb7`, type-info `0x0091719d` | Single `u32`. The source oplist comments out this opcode, but the recovered row precedes the known `OP_MailGetMessageMsg`/`OP_MailSendMessageMsg` pair. |
| `271` | likely `OP_MailGetMessageMsg` | `0x00925091` serialize, `0x009250dc` deserialize; factory `0x0091cff4`, type-info `0x009171e3` | `u64`, `u32`, `u32`, `u8`. |
| `272` | likely `OP_MailSendMessageMsg` | `0x00925127` serialize, `0x00925193` deserialize; attachment helper `0x00993725`/`0x0099383b` | `string16 player_to`, `string16 subject`, `string16 mail_body`, `u8 send_flag`, `u64`, `u32`, then shared mail attachment payload. Attachment payload is four coin `u32` values, `u16 item_packet_type_or_end_tag`, a direct discriminator-0 item-detail variant body, `u32 blob_size`, and raw blob clamped to `0x1000` bytes. `server/WorldStructs.xml` now has a v546 prefix row through the fixed `u64/u32` fields, which is enough for the current server mail-send validation while leaving the item-detail attachment tail for a later dedicated item-helper pass. |
| `273` | likely `OP_MailDeleteMessageMsg` | `0x009251ff` serialize, `0x0092522c` deserialize; factory `0x0091d042`, type-info `0x00917230` | Two `u32` values. |
| `274` | `OP_MailGetHeadersReplyMsg` | `0x00929e27` serialize, `0x0094ade4` deserialize; record helper `0x009937aa`/`0x009938c5`; handler `0x004754f0` | `u32 kiosk_id`, `u8 message_count`, then shared mail records: `mail_id`, `player_to_id`, three context-dependent `string16` fields, `already_read_or_unknown1`, `mail_deletion_or_unknown2`, `mail_type_or_lock_report_button`, `mail_expire_or_unknown3`, and shared attachment payload (`coins`, `u16`, direct item-detail variant 0, bounded raw blob); followed by `postage_cost`, `attachment_cost`, and one unknown trailing `u32`. |
| `275` | `OP_MailGetMessageReplyMsg` | `0x00925259` serialize, `0x009252a4` deserialize; handler `0x00475550` | `u32 kiosk_id`, one shared mail message record including the shared attachment payload (`coins`, `u16`, direct item-detail variant 0, bounded raw blob), `u64`, and trailing `u8`. In full-message context the three record strings align with `player_from`, `subject`, and `mail_body`; in header-list context the third string is usually an empty/unknown field. |
| `276` | `OP_MailSendMessageReplyMsg` | `0x009252ef` serialize, `0x00925362` deserialize; handler `0x004755c0` | `u64`, `u32`, `u8 reply_type`, `string16`, `u32`, `u8`, `u32`. The v546 XML row now names those fixed fields directly instead of using 12 raw bytes before `reply_type` and 9 raw bytes after the name. |
| `277` | likely `OP_MailCommitSendMessageMsg` | `0x009253d5` serialize, `0x0092541e` deserialize | `u64`, `u32`, `string16`, `u32`. |
| `278` | likely `OP_MailSendSystemMessageMsg` | `0x00925467` serialize, `0x009254c2` deserialize | Four `string16` values, `u64`, then the shared mail attachment payload (`coins`, `u16`, direct item-detail variant 0, bounded raw blob). |
| `279` | likely `OP_MailRemoveAttachFromMailMsg` | `0x0092551d` serialize, `0x00925557` deserialize | `string16`, `u32`, `u32`. |
| `280` | likely `OP_WaypointRequestMsg` | `0x0091790c` serialize, `0x00917911` deserialize | Stub functions return true; no custom payload fields. |
| `281` | `VeWaypointReplyMsg` / `OP_WaypointReplyMsg` | `0x009296f2` serialize, `0x009473fc` deserialize; entry helper `0x00924149`/`0x0092417e`; UI handler `0x006e39b0` | `u32 count`, entries of `string16`, `u8`, `u32`, and trailing `u32`. Deserializer rejects counts `>= 0x10001`. |
| `282` | `OP_WaypointSelectMsg` | `0x009241fa` serialize, `0x00924212` deserialize; factory `0x0091c955`, type-info `0x006e1ee0`; handler `0x004753f0` | Single `u32`. The handler applies this as the selected waypoint id in the `Waypoint` UI. |
| `283` | `VeWaypointUpdateMsg` / `OP_WaypointUpdateMsg` | `0x00929754` serialize, `0x00947490` deserialize; UI handler `0x006e3d70` | Same waypoint entries as type `281`, followed by trailing `u8` and `u32`. Deserializer rejects counts `>= 0x10001`. v546 XML now includes the trailing one-byte flag before the existing `unknown` `u32`. |
| `284` | likely `OP_CharNameChangedMsg` | `0x0092422a` serialize, `0x00924280` deserialize | Two `string16` values followed by three `u32` values. |
| `285` | `VeShowZoneTeleporterDestinationsMsg` / `OP_ShowZoneTeleporterDestinationsMsg` | `0x009297c5` serialize, `0x00947533` deserialize; handler `0x00475430` | `u32`, `u32 destination_count`, entries of `u32`, `string16`, `string16`, `u32`. Deserializer rejects destination counts `>= 0x41`. The handler rebuilds the `ZoneTeleporter` UI destination list. |
| `286` | likely `OP_SelectZoneTeleporterDestinationMsg` | `0x009242d6` serialize, `0x0092431f` deserialize | `u32`, `u32`, `string16`, `u32`. |
| `287` | likely `OP_ReloadLocalizedTxtMsg` | `0x0091929a` serialize, `0x0091929f` deserialize | Stub functions return true; no custom payload fields. |
| `288` | likely `OP_RequestGuildMembershipMsg` | `0x00924368` serialize, `0x00924395` deserialize | Two `u32` values. |
| `289` | `VeGuildMembershipResponseMsg` / `OP_GuildMembershipResponseMsg` | `0x009372bd` serialize, `0x00945105` deserialize; member helper `0x00930eaf`/`0x00931010`; handler `0x004afb50` | `u32 guild_id`, `u32 target_character_id`, `u32 member_count`, guild member records, trailing `u16`. Member records include id/name fields, class/level/rank/status fields, zone/note/officer-note strings, and trailing account id. Deserializer rejects counts `>= 0x10001`; v546 XML now exposes the recovered recruiter/points/account fields instead of unnamed padding. |
| `290` | `OP_LeaveGuildNotifyMsg` | `0x009243c2` serialize, `0x009243fe` deserialize; factory `0x0091c9e5`, type-info `0x00916bfc`; handler `0x004a1600` | `u32`, `u32`, `u16`; v546 XML now includes the trailing `u16`, with a 562 row preserving the previous two-field body. |
| `291` | `OP_JoinGuildNotifyMsg` | `0x00937349` serialize, `0x00937382` deserialize; handler `0x004ab080` | `u32 guild_id`, one shared guild member record, trailing `u16`. v546 XML now exposes the same recovered recruiter/points/account fields as the membership response row, with a 562 row preserving the previous layout for later clients. |
| `292` | likely `OP_AvatarUpdateMsg` | `0x0092443a` serialize, `0x009244ae` deserialize; factory `0x0091ca22`, type-info `0x00916c3d` | Seven `u32` values. `server/WorldStructs.xml` now has a v546 `WS_InstanceUpdate` override for this fixed body instead of inheriting the string-heavy legacy row. |
| `293` | `OP_BioUpdateMsg` | `0x009192a4` serialize, `0x009192ba` deserialize; handler `0x00486fc0` | Single `string16` biography field; matches PacketParser `WS_BioUpdate` shape. The handler routes the string through the `InspectPlayer` UI. |
| `294` | `OP_QuestReward` | `0x00924522` serialize, `0x00924549` deserialize; payload helper `0x0091abb2`/`0x0091ad8c`; handler `0x00475450` | Leading `u8` wrapper flag, then quest-reward payload matching the PacketParser `WS_QuestComplete` family: title/name/description-style strings, coin/status/XP fields, reward item lists, and faction string/amount list. The handler uses the flag to choose widget/reward handling or the `InspectPlayer` reward UI path. |
| `295` | likely `OP_WSServerLockMsg` | `0x00924570` serialize, `0x00924588` deserialize; factory `0x0091ca5f`, type-info `0x00916c8a` | Single `u8`. Name follows the source/PacketParser lock-row order before `OP_CsCategoryRequestMsg`. |
| `296` | likely `OP_LSServerLockMsg` | `0x009245d0` serialize, `0x009245e8` deserialize; factory `0x0091cad9`, type-info `0x00916cf4` | Single `u8`. PacketParser places this before `OP_WSServerHideMsg`; the legacy source oplist places it after. |
| `297` | likely `OP_WSServerHideMsg` | `0x009245a0` serialize, `0x009245b8` deserialize; factory `0x0091ca9c`, type-info `0x00916cbf` | Single `u8`. PacketParser/source agree on the surrounding lock/hide cluster, but differ on this row's ordering versus `OP_LSServerLockMsg`. |
| `298` | likely `OP_CsCategoryRequestMsg` | `0x00924600` serialize, `0x00924618` deserialize; factory `0x0091cb16`, type-info `0x005b9330` | Single `u32`, matching the request slot immediately before `VeCsCategoryResponseMsg`. |
| `299` | `VeCsCategoryResponseMsg` / `OP_CsCategoryResponseMsg` | `0x0092985f` serialize, `0x00948e77` deserialize; handler `0x004734f0` | `u32`, `u32 category_count`; each category has `string16`, `u32`, `u32 nested_count`, then nested entries of `string16`, `u32`. Deserializer rejects category counts `>= 0x1001`. The handler forwards the category tree to the `Help` UI. |
| `300` | `OP_KnowledgeWindowSlotMappingMsg` | `0x0092baad` serialize, `0x0092fbec` deserialize; handler `0x00473980` | `u16 spell_count`, then `spell_count` entries of `u32 spell_id` and `u16 slot_id`; matches PacketParser `WS_SpellSlotMapping`. |
| `301` | `OP_LFGUpdateMsg` | `0x00924630` serialize, `0x0092465d` deserialize; factory `0x0091cb7f`, type-info `0x00916da6`; handler `0x00473510` | Two `u8` values. The handler resolves the `GroupMembers` UI and applies the state through its virtual update method. |
| `302` | likely `OP_AFKUpdateMsg` | `0x0092468a` serialize, `0x009246c4` deserialize | `u32`, `u8`, `string16`. |
| `303` | likely `OP_AnonUpdateMsg` | `0x009246fe` serialize, `0x0092473a` deserialize; factory `0x0091cbbc`, type-info `0x00916df3` | `u32`, `u8`, `u8`. |
| `304` | `VeUpdateActivePublicZonesMsg` / likely `OP_UpdateActivePublicZonesMsg` | `0x0092bb10` serialize, `0x00948f9a` deserialize | `u32 zone_count`; each zone carries `string16`, `u32 group_count`, and groups with `string16`, `u8`, `u8`, plus an optional `u32 count`/`u32[]` list when the second flag is nonzero. Deserializer rejects zone counts `>= 0x10001` and group counts above `0x1000`. |
| `305` | likely `OP_UnknownNpcMsg` | `0x00924776` serialize, `0x009247d0` deserialize | Five `u32` values. |
| `306` | `VePromoFlagsDetailsMsg` / `OP_PromoFlagsDetailsMsg` | `0x0092bc31` serialize, `0x0094a490` deserialize; handler `0x00473540` | `u32 item_count`; each promo entry is `u32`, `u8`, `u32`, `u8`, two `string16` values, `u8 subentry_count`, and subentries of `u32`, `u8`; then a trailing `u16` count of discriminator-0 item-detail variant records. Deserializer rejects item counts `>= 0x401`. The handler loads the list into the `Claim` UI and prints the no-more-items text for empty lists. |
| `307` | `VeConsignViewCreateMsg` / likely `OP_ConsignViewCreateMsg` | `0x009299fd` serialize, `0x00943618` deserialize; header helper `0x009929ee`/`0x00992ad4` | `u64`, three `u32`, a large consignment header helper, `u32 skill_count`, skill entries of `string16`, `u32`, and trailing `u32`. Deserializer rejects skill counts `>= 0x10001`. |
| `308` | likely `OP_ConsignViewGetPageMsg` | `0x0092482a` serialize, `0x00924884` deserialize | `u64`, four `u32` values. |
| `309` | likely `OP_ConsignViewReleaseMsg` | `0x009248de` serialize, `0x0092490b` deserialize | `u64`, `u32`. |
| `310` | likely `OP_ConsignRemoveItemsMsg` | `0x00924a3c` serialize, `0x00924a78` deserialize | `u64`, two `u32` values. |
| `311` | `OP_UpdateDebugRadiiMsg` | `0x00924938` serialize, `0x009249ba` deserialize; factory `0x00927ee9`, type-info `0x0091ccc5` | Six `u32` values, raw `0x0c` block, trailing `u32`. |
| `312` | `OP_SnoopMsg` | `0x00924ab4` serialize, `0x00924b14` deserialize; factory `0x00939936`, type-info `0x00934f02` | `u32`, `u32`, `u8`, `string16`, trailing `u8`. The source/PacketParser order places Snoop between `OP_UpdateDebugRadiiMsg` and `OP_ReportMsg`; the legacy `emu_oplist.h` row is commented out. |
| `313` | `OP_ReportMsg` | `0x009192d0` serialize, `0x009192f9` deserialize; factory `0x00939968`, type-info `0x004b7a00` | Two `string16` values. |
| `314` | `VeUpdateRaidMsg` / `OP_UpdateRaidMsg` | `0x00924b78` serialize, `0x00924baf` deserialize; factory `0x0091cd57`, type-info `0x00916f65`; handler `0x004739a0` | `u32 packed_size`, then an EQ2Emu `Pack()` zero-run/literal stream. Server-side `PlayerInfo::serializeRaid` packs the full v546 `WS_RaidUpdate` on first send, then packs XOR deltas on later sends. The v546 unpacked body is 24 `Substruct_RaidMember` slots of 137 bytes: `zone_status`, fixed 41-byte `name`, `spawn_id`, `pet_id`, current/max level, race/class, signed HP/power current/max, trauma/arcane/noxious/elemental counters, fixed 60-byte `zone`, and `instance`. Deserializer rejects packed sizes above `0x1000`. The handler decodes into the cached raid sheet at world-data `+0x13bc`. The constructed factory vtable is `0x00d34ff0`; older extracted registry artifacts that scanned past the short factory body incorrectly reported the shared empty vtable. |
| `315` | `VeUpdateArenaMsg` / `OP_UpdateArenaMsg` | `0x00973de5` serialize, `0x00973e1c` deserialize; factory `0x00973b08`, type-info `0x00973524`; handler `0x00473a70` | `u32 blob_size`, then raw arena-update blob bytes. Deserializer rejects sizes above `0x1000`. The handler decodes into the cached arena sheet at world-data `+0x2098`. |
| `316` | `OP_ConsignViewSortMsg` | `0x00924c16` serialize, `0x00924c7e` deserialize; factory `0x0091cc42`, type-info `0x00916e8d` | `u64`, `u32`, `u32`, `u64`, `u32`, `u32`. |
| `317` | `OP_TitleUpdateMsg` | `0x00924ce6` serialize, `0x009476da` deserialize; factory `0x00943350`, type-info `0x0093ffda`; handler `0x00473650` | `u16 title_count`, entries of `string16`, `u8`, then two trailing `u16` values; this is the same 4-byte tail size as the later PacketParser `WS_TitleUpdate` trailing `u32`. The handler resolves the `InspectPlayer` UI and applies the update there. |
| `318` | `OP_ClientFellMsg` | `0x00924d65` serialize, `0x00924d92` deserialize; factory `0x0091cd9e`, type-info `0x0050ae60` | Two 4-byte values: fall `height` as float/raw `u32`, then `spawn_id`. Matches legacy `WS_ClientFell`. |
| `319` | `OP_ClientInDeathRegionMsg` | `0x00917916` serialize, `0x0091791b` deserialize; factory `0x0091cdb4`, type-info `0x0050ae10` | Stub serializer/deserializer; no custom payload. |
| `320` | `OP_CampClientMsg` | `0x00924dbf` serialize, `0x00924dd7` deserialize; factory `0x0091cdca`, type-info `0x00916fca` | Single `u32` field. |
| `321` | `OP_CSToolAccessResponseMsg` | `0x00924e1f` serialize, `0x00924e4c` deserialize; factory `0x0091ce44`, type-info `0x00917034` | `u32`, trailing `u8` response/flag. |
| `322` | `OP_DeleteGuildMsg` | `0x00924def` serialize, `0x00924e07` deserialize; factory `0x0091ce07`, type-info `0x00916fff` | Single `u32` field. The legacy source oplist keeps this row between `OP_CSToolAccessResponseMsg` and `OP_TrackingUpdateMsg`; PacketParser v546 metadata does not keep it in this range. |
| `323` | `OP_TrackingUpdateMsg` | `0x00929b6c` serialize, `0x00947815` deserialize; factory `0x0094342a`, type-info `0x0094006c`; handler `0x0048eba0` | `u8 mode`, packed count of spawn records (`u32`, `string8`, `u8`, `u8`), packed count of `u32` ids, then packed count of list-order entries (`u32`, packed `u16`). The handler switches on mode byte `+0x04` for stop/search/list/start behavior. v546 XML now uses packed `OversizedValue=127` counters and omits the later-client `type` byte; a 562 row preserves the previous layout. |
| `324` | `OP_BeginTrackingMsg` | `0x00924ea9` serialize, `0x00924ec1` deserialize; factory `0x0091cebe`, type-info `0x006c6870` | Single `u32 spawn_id`; matches legacy `WS_BeginTracking`. |
| `325` | `OP_StopTrackingMsg` | `0x00917920` serialize, `0x00917925` deserialize; factory `0x0091ced4`, type-info `0x006c68c0` | Stub serializer/deserializer; no custom payload. |
| `326` | `OP_GetAvatarAccessRequestForCSToolsMsg` | `0x0091792a` serialize, `0x0091792f` deserialize; factory `0x0091ceea`, type-info `0x00541770` | Stub serializer/deserializer; no custom payload. |
| `327` | `OP_AdvancementRequestMsg` | `0x00924ed9` serialize, `0x00924ef1` deserialize; factory `0x0091cf00`, type-info `0x009170e6`; inline GameScene case `0x004b304b` | Raw 8-byte payload, consistent with legacy XML's `int8[1]` plus `int8[7]` advancement request body. The dispatch case copies the two `u32` halves into global advancement-request state. |
| `328` | `OP_MapFogDataInitMsg` | `0x00929c8c` serialize, `0x0094a9d4` deserialize; helpers `0x0091a91c`/`0x00930d51`, `0x00927bce`/`0x00947dd0` | Handler-backed at `0x00474e20`. Body is two `u32`, `u8` fog-location count, fog-location records, `u8` map-record count, and map records. Location records are `u32`, `string16`, four `u32`, two packed `u16`, packed blob size plus blob. Map records have two `string16`, eight `u32`, packed `u16`, `u64`, and counted subentries. Do not apply PacketParser numeric opcode `328`/`OP_ReloadLocalizedTxtMsg`; that empty row is already mapped at client type `287`. |
| `329` | `OP_MapFogDataUpdateMsg` | `0x00929d4d` serialize, `0x0094832d` deserialize | `u8` fog-location count followed by the same compact fog-location records used by type `328`. This matches the source/PacketParser MapFogDataUpdate slot after the handler-backed init row; GameScene does not install an inbound handler for this client-to-server style update row. |

EQ2Emu implementation note: `WS_FogInit` for `ClientVersion="546"` now follows
the recovered type `328` layout instead of the old tiny `lowest_z/highest_z`
stub. `WorldDatabase::LoadFogInit` already fills `map_id`, `num_maps`, map
names, bounds, and keys, so v546 map-fog replies no longer serialize a body that
the client would interpret as a nonzero fog-location list followed by missing
records. `PacketStruct::GetOpcodeValue` also overrides the confirmed v546 root
ids for `OP_ReloadLocalizedTxtMsg` (`287`), `OP_MapFogDataInitMsg` (`328`), and
`OP_MapFogDataUpdateMsg` (`329`) before falling back to the PacketParser-derived
opcode table; `EQ2Packet::PreparePacket` applies the same override on the actual
wire send path.
| `330` | `OP_CloseGroupInviteWindowMsg` | `0x00917934` serialize, `0x00917939` deserialize; factory `0x0091cf3d`, type-info `0x0091711b` | Handler-backed at `0x0046c4e0`; empty payload closes the cached group-invite window object and resets the stored window handle. |
| `331` | likely `OP_CorruptedClientMsg` | `0x00924f09` serialize, `0x00924f70` deserialize; factory `0x0091cf7a`, type-info `0x00917150` | Six `u32` values. Name follows nearby PacketParser/source ordering. |
| `332` | likely `OP_WorldDataChangeMsg` | `0x0091da85` serialize, `0x0091daa3` deserialize; factory `0x0091b1a7`, type-info `0x0091530e` | Single `u8` value. Name follows nearby PacketParser/source ordering. |
| `333` | `OP_MailEventNotificationMsg` | `0x00924fd7` serialize, `0x0092501c` deserialize; factory `0x0093999a`, type-info `0x0093500e` | `u8` followed by three `string16` values. |
| `334` | `OP_OfferQuestMsg` | `0x00929da3` serialize, `0x0094ad3a` deserialize; factory `0x0094a45b`, type-info `0x0094a087`; handler `0x0048edd0` | Thin wrapper around the reward-pack payload helper, then `string8`, three `u8` flags, `string8`, `string8`, and `string16`. The handler uses the `RewardPack` path when available, otherwise builds and opens the quest-offer accept dialog; v546 XML now uses the recovered reward-pack wrapper shape instead of inheriting the older 374-era body. |
| `335` | likely `OP_RestartZoneMsg` | `0x00925591` serialize, `0x009255c8` deserialize; factory `0x0091d07f`, type-info `0x009172a9` | `u8`, `u32`. Name follows nearby PacketParser ordering; semantic fields still need handler evidence. |
| `336` | `OP_DisplayMailScreenMsg` | `0x00925678` serialize, `0x00925690` deserialize; factory `0x0091d0c4`, type-info `0x009172de` | Handler-backed at `0x0046cd10`; single `u32` body. The handler logs the display-mail-screen message. |
| `337` | likely `OP_CharacterLinkdeadMsg` | `0x0091793e` serialize, `0x00917943` deserialize | Stub functions return true; no custom payload fields. |
| `338` | likely `OP_CharTransferStartRequestMsg` | `0x00917948` serialize, `0x0091794d` deserialize | Stub functions return true; no custom payload fields. |
| `339` | char-transfer base plus string row | `0x009208e3` serialize, `0x00920905` deserialize; base helper `0x009206f0`/`0x00920773` | Shared base fields `u64`, four `u32`, `u64`, `u8`, `u64`, then trailing `string16`. |
| `340`-`341` | char-transfer request rows | `0x00925604`/`0x0092563e`, `0x009256a8`/`0x009256e2` | Both rows are `u32`, `u32`, `string16`. Likely rollback/commit request pair. |
| `342` | char-transfer reply-shaped row | `0x00922b08` serialize, `0x00922b4d` deserialize | Three `string16` values followed by `u64`. |
| `343` | `OP_CharTransferCommitReplyMsg` candidate | `0x0092571c` serialize, `0x00925774` deserialize; factory `0x0091d101`, type-info `0x0091732b` | `u8`, `u32`, `u32`, `u8`, `u32`. Name follows PacketParser/source ordering; semantic fields still need handler evidence. |
| `344` | u32/string transfer-adjacent row | `0x009257cc` serialize, `0x009257f7` deserialize | `u32`, `string16`. |
| `345` | `OP_FlightPathsMsg` | `0x00929ac7` serialize, `0x0094776f` deserialize | Handler-backed at `0x004afc50`; `u16 route_count`, then `route_count` `u16` route lengths, followed by nested raw `0x0c`-byte coordinate triplets. Matches the legacy `WS_FlightPathsMsg` route-list body. |
| `346` | u32/string transfer-adjacent row | `0x00926525` serialize, `0x00926550` deserialize | `u32`, `string16`. |
| `347` | char-transfer common-only row | `0x00925822` serialize, `0x00925833` deserialize; common helper `0x0091afce`/`0x0091b0ad` | Common helper writes `u32`, `u32`, `string16`, `u32`, `string16`, `u8`, `string16`, `u32`, `string16`, `u8`, two `u32`, `string16`, and two trailing `u32`. |
| `348` | `VeCharTransferStartReplyMsg` | `0x00925844` serialize, `0x009373bb` deserialize | `u8`, char-transfer common helper, then four `u32 blob_size` plus raw blob sections. Deserializer rejects each blob size `>= 0x100001`. |
| `349` | `VeCharTransferRequestMsg` | `0x00925941` serialize, `0x00937524` deserialize | Char-transfer common helper followed by four `u32 blob_size` plus raw blob sections. Deserializer rejects each blob size `>= 0x100001`. |
| `350` | char-transfer common with leading flag | `0x00925a31` serialize, `0x00925a58` deserialize | `u8` followed by char-transfer common helper. |
| `351` | char-transfer common with trailing flag | `0x00925a7f` serialize, `0x00925aa9` deserialize | Char-transfer common helper followed by trailing `u8`. |
| `352` | char-transfer common-only row | `0x00925ad3` serialize, `0x00925ae4` deserialize | Char-transfer common helper only. |
| `353` | transfer-adjacent scalar row | `0x00925af5` serialize, `0x00925b22` deserialize; factory `0x0091d13e`, type-info `0x009173b4` | `u32`, `u8`. Exact opcode label still needs handler/order evidence. |
| `354` | transfer-adjacent scalar row | `0x00925b4f` serialize, `0x00925b67` deserialize; factory `0x0091d17b`, type-info `0x009173e9` | Single `u32`. Exact opcode label still needs handler/order evidence. |
| `355` | char-transfer envelope row | `0x00925b7f` serialize, `0x00925bee` deserialize | `u8`, four `u32`, `string16`, then char-transfer common helper. |
| `356` | `VeGetCharacterSerializedReplyMsg` | `0x00925c5d` serialize, `0x0093767e` deserialize | Two `u8`, `u32`, char-transfer common helper, two `u32`, `string16`, and four size-prefixed raw blobs. Deserializer rejects blob sizes `>= 0x100001`. |
| `357` | `VeCreateCharFromCBBRequestMsg` | `0x00925da7` serialize, `0x00937838` deserialize | `u8`, `string16`, `u64`, three `u32`, char-transfer common helper, and four size-prefixed raw blobs. Deserializer rejects blob sizes `>= 0x100001`. |
| `358` | char-transfer validation-shaped row | `0x00925ee9` serialize, `0x00925f61` deserialize | Two `u8`, `u32`, `u64`, two `u32`, then char-transfer common helper. Exact opcode label remains unresolved; the stronger PacketParser/source validate-request match is the post-auction row `369`. |
| `359` | likely `OP_HousingDataChangedMsg` | `0x00929ebd` serialize, `0x00946350` deserialize | `u32 count` followed by `count` `string16` values. |
| `360` | `VeHousingRestoreMsg` / likely `OP_HousingRestoreMsg` | `0x0092bdca` serialize, `0x0093ce9d` deserialize | `u32 count`, string16 list, `string16`, two `u32`, `u32 blob_size` plus raw blob, and trailing `u8`. Deserializer rejects blob sizes `>= 0x100001`. |
| `361` | likely `OP_AuctionItem` | `0x00925fd9` serialize, `0x00926031` deserialize | `u32`, `u32`, `u64`, `u32`, `string16`. Name follows the source/PacketParser auction block immediately after `HousingRestore`; field semantics still need handler evidence. |
| `362` | likely `OP_AuctionItemReply` | `0x00926089` serialize, `0x009260f0` deserialize; factory `0x0091d1b8`, type-info `0x0091747e` | `u8`, `u32`, `u32`, `u64`, `u32`, `u64`. Name follows the source/PacketParser auction block; field semantics still need handler evidence. |
| `363` | likely `OP_AuctionCoin` | `0x00926157` serialize, `0x009261a0` deserialize | `u32`, `u32`, `u64`, `string16`. Name follows the source/PacketParser auction block; field semantics still need handler evidence. |
| `364` | likely `OP_AuctionCoinReply` | `0x009261e9` serialize, `0x00926242` deserialize; factory `0x0091d1f5`, type-info `0x009174bf` | `u8`, `u32`, `u32`, `u64`, `u64`. Name follows the source/PacketParser auction block; field semantics still need handler evidence. |
| `365` | likely `OP_AuctionCharacter` | `0x0092629b` serialize, `0x009262f3` deserialize | `u32`, `u64`, `u32`, `u32`, then a string using the stream helper with a 3-byte length-size parameter. Name follows the source/PacketParser auction block; field semantics still need handler evidence. |
| `366` | `OP_AuctionCharacterReply` | `0x0092634b` serialize, `0x00926396` deserialize; factory `0x0091d232`, type-info `0x00917500`; handler `0x00475c30` | `u8`, `u32`, `u32`, `u64`. The handler uses the status byte for Station Exchange character-transfer/auction error text and clears/refreshes the `Exchange` UI when present. |
| `367` | likely `OP_AuctionCommitMsg` | `0x009263e1` serialize, `0x0092640e` deserialize; factory `0x0091d26f`, type-info `0x00917535` | `u64`, `u32`. Name follows the source/PacketParser auction block; field semantics still need handler evidence. |
| `368` | likely `OP_AuctionAbortMsg` | `0x0092643b` serialize, `0x00926453` deserialize; factory `0x0091d2ac`, type-info `0x0091756a` | Single `u64`. Name follows the source/PacketParser auction block; field semantics still need handler evidence. |
| `369` | likely `OP_CharTransferValidateRequestMsg` | `0x0092646b` serialize, `0x00926492` deserialize | `u8`, then the shared char-transfer common helper. Name follows the source/PacketParser block order immediately after the auction rows; field semantics still need handler evidence. |
| `370` | likely `OP_CharTransferValidateReplyMsg` | `0x009264b9` serialize, `0x009264ef` deserialize | Two `u8` values, then the shared char-transfer common helper. Name follows the source/PacketParser block order immediately after the auction rows; field semantics still need handler evidence. |
| `371` | likely `OP_RaceRestrictionMsg` | `0x0092657b` serialize, `0x009265a8` deserialize; factory `0x0091d2e9`, type-info `0x009175b7` | Two `u8` values. Name follows the source/PacketParser post-auction block order; semantic fields still need handler evidence. |
| `372` | likely `OP_SetInstanceDisplayNameMsg` | `0x009265d5` serialize, `0x00926600` deserialize | `u32`, `string8`. Name follows the source/PacketParser post-auction block order; semantic fields still need handler evidence. |
| `373` | likely `OP_GetAuctionAssetIDMsg` | `0x0092662b` serialize, `0x00926658` deserialize; factory `0x0091d326`, type-info `0x004b38b0` | Two `u32` values. Name follows the source/PacketParser post-auction block order; semantic fields still need handler evidence. |
| `374` | `OP_GetAuctionAssetIDReplyMsg` | `0x009266db` serialize, `0x00926726` deserialize; factory `0x0091d33c`, type-info `0x00917604`; handler `0x0049db10` | `u8`, `u64`, `u32`, `u32`. Status `0` enters the Station Exchange asset-id success path; nonzero statuses display Station Exchange eligibility/error text. |
| `375` | likely `OP_ResendWorldChannelsMsg` | `0x00926685` serialize, `0x009266b0` deserialize | `u32`, `string16`. Name follows the source/PacketParser post-auction block order; semantic fields still need handler evidence. |
| `376` | `OP_DisplayExchangeScreenMsg` | `0x00917952` serialize, `0x00917957` deserialize; handler `0x00475c00` | Stub functions return true; no custom payload fields. The handler opens/refreshes the `Exchange` UI. |
| `377` | likely `OP_ArenaGameTypesMsg` | `0x00926771` serialize, `0x009267ab` deserialize | `u32`, `u64`, `string16`. Name follows the source/PacketParser post-auction block order; semantic fields still need handler evidence. |
| `378` | `OP_EqHearChatCmd` | `0x00974874` serialize, `0x009763d2` deserialize; helpers `0x009774ec`/`0x00979a53`, `0x0097761f`/`0x0097a9b3`; handler `0x004b26f0` | Header helper writes `u8`, three `u8`-count lists of `string16,string8` entries, and trailing `u8`; top-level then writes `u8` grouped-entry count. Grouped entries are two `u8`, `string8`, `string16`, a `u8` count of `string16,string8` entries, one `u8`, and a `u8` count of nested string-list helpers. The handler applies Eq command helpers and routes through `ArenaMain` when present. |
| `379` | `OP_EqDisplayTextCmd` | `0x009748d3` serialize, `0x00975dee` deserialize; helper `0x00977172`/`0x009771cf`; handler `0x00473870` | `u32`, packed count, then entries of `u32`, `string8`, and four `u8` values. The handler routes the text command list through `ArenaMain`. |
| `380` | `OP_EqCreateGhostCmd` | `0x009740b8` serialize, `0x00974140` deserialize; handler `0x004738b0` | `u8 action`; action `0` carries the short-entry helper, action `1` carries raw `u32`, action `2` carries `u32` plus `u8`; other actions assert `"Unhandled action"`. The handler switches on the same action byte for ArenaMain ghost state. |
| `381` | `OP_EqCreateWidgetCmd` | `0x00973e83` serialize, `0x00973eb9` deserialize; helper `0x00977054`/`0x009770e3`; inline GameScene case `0x004b30d4` | Two `u32` values followed by a visual-state helper: `string8`, `string8`, and eight `u8` fields. The dispatch case resolves `ArenaMain` and applies the widget payload through `0x0054caf0`. |
| `382` | arena zone/status list | `0x00974927` serialize, `0x00975e4f` deserialize; handler `0x00479c40` | `u32`, `u16 count`, then `count` `string8` values. The handler prints the arena-zone count and names to chat/status output, so the source-order `OP_EqCreateSignWidgetCmd` label is provisional. |
| `383` | `OP_EqDestroyGhostCmd` | `0x00973f3d` serialize, `0x00973f82` deserialize; factory `0x00973b4f`, type-info `0x0046a780`; handler `0x004a9130` | Three `u32` values. The handler routes the ghost/widget command through Arena UI helpers. |
| `384` | likely `OP_EqUpdateGhostCmd` | `0x00973fc7` serialize, `0x00974041` deserialize; widget helper `0x0097779e`/`0x0097ad31` | Source-order Eq command candidate. Two `u32`, `u32 id`, `string8`, `u8`, widget payload helper, and packed `u16` tail. The widget helper writes `u32`, `string8`, `string16`, a packed-string8 entry, `u8` count of default widget subrecords, `u8` count of `string8,string16,u8` entries, and three 3-byte color-like blocks. Default subrecords use vtable `0x00cd78c8` and have a fixed scalar/list/tail-string layout. |
| `385` | likely `OP_EqSetControlGhostCmd` | `0x009741c8` serialize, `0x009741fc` deserialize | Source-order Eq command candidate. `string16`, `u32`, then the visual-state helper. |
| `386` | likely `OP_EqSetPOVGhostCmd` | `0x00973926` serialize, `0x00973937` deserialize; helper `0x00977a39`/`0x0097b114` | Source-order Eq command candidate. Complex visual-list helper only: `u32`, visual-state helper, `u32`, `u8`, `u32`, a `u8` count of string/map records, a packed count of `u32` IDs, and a packed count of widget records. String/map records carry `string8`, `u32`, `u8`, packed `u16`, then a `u8` count of `u32` IDs. |
| `387` | likely `OP_EqHearCombatCmd` | `0x00973eef` serialize, `0x00973f16` deserialize | Source-order Eq command candidate. `u32`, then the visual-state helper. |
| `388` | likely `OP_EqHearSpellCastCmd` | `0x00974406` serialize, `0x00974467` deserialize | Source-order Eq command candidate. Two `u32`, two `string8` values, widget payload helper, and packed `u16` tail. |
| `389` | `OP_EqHearSpellInterruptCmd` | `0x009745d1` serialize, `0x009745f8` deserialize; handler `0x00473920` | `u32`, then the complex visual-list helper. The handler applies the visual-list payload to Arena score/respawn UI helpers. |
| `390` | `OP_EqHearSpellFizzleCmd` | `0x00974230` serialize, `0x0097431b` deserialize; handler `0x004b0d20` | Two `u32`, `u8 action`; cases `0`/`4` use the widget-record helper (`u32`, `string8`, widget payload, three `u8` flags), while cases `1`, `2`, `3`, `5`, `6`, `7`, `8`, and `9` carry small raw scalar payloads. The handler switches on the same action byte for arena visual/revive/crowd/sound state. |
| `391` | likely `OP_EqHearConsiderCmd` | `0x0097451f` serialize, `0x00974578` deserialize; factory `0x00973b65`, type-info `0x009735f5` | Source-order Eq command candidate. Three `u32` values, `u8`, and `u16`. |
| `392` | likely `OP_EqUpdateSubClassesCmd` | `0x00973948` serialize, `0x00973959` deserialize | Source-order Eq command candidate. Thin wrapper around the shared `EqCmdWidgetPayload` helper: `u32`, `string8`, `string16`, packed-string8 entry, `u8` count of default widget subrecords, `u8` count of `string8,string16,u8` entries, and three 3-byte color-like blocks. |
| `393` | `OP_EqCreateListBoxCmd` | `0x009749a7` serialize, `0x00975ec8` deserialize; helper `0x00977292`/`0x009772b8`; handler `0x004ab3e0` | `u8 count`, then entries of `u32`, `string8`. Serializer decompile obscures the count assignment, but the deserialize path confirms the on-wire count byte. The handler updates ArenaScore list-box data. |
| `394` | likely `OP_EqSetDebugPathPointsCmd` | `0x009746e9` serialize, `0x0097472e` deserialize | Source-order Eq command candidate. Three `u32` values followed by the shared `EqCmdWidgetPayload` helper. |
| `395` | likely `OP_EqCannedEmoteCmd` | `0x0097461f` serialize, `0x0097464c` deserialize | Source-order Eq command candidate. Two `u32` values. |
| `396` | likely `OP_EqStateCmd` | `0x009744c5` serialize, `0x009744f2` deserialize | Source-order Eq command candidate. Two `u32` values. |
| `397` | likely `OP_EqPlaySoundCmd` | `0x00974679` serialize, `0x009746b1` deserialize | Source-order Eq command candidate. `u32`, `string16`, `string16`. |
| `398` | empty Arena UI command | `0x009737f2` serialize, `0x009737f7` deserialize; inline GameScene case `0x004b31a6` | Stub functions return true; no custom payload fields. The dispatch case resolves `ArenaMain` and toggles local player/UI control state through `0x006e7230`; the source-order `OP_EqPlaySound3DCmd` label is provisional. |
| `399` | empty Arena UI command | `0x009737fc` serialize, `0x00973801` deserialize; inline GameScene case `0x004b31d6` | Stub functions return true; no custom payload fields. The dispatch case resolves visible `ArenaMain` state and toggles Arena UI control state through `0x006e63b0`; the source-order `OP_EqPlayVoiceCmd` label is provisional. |
| `400` | `OP_EqHearDrowningCmd` | `0x00974773` serialize, `0x0097478b` deserialize; factory `0x00973da8`, type-info `0x009737d2`; inline GameScene case `0x004b3215` | Single `u32`. The dispatch case resolves `ArenaRevive` and routes the id into the `KilledByText`/revive text path. |
| `401` | likely `OP_InviteRequestMsg` | `0x009267e5` serialize, `0x00926872` deserialize | `u8`, `u32`, `string16`, `string16`, `u32`, `string16`, two `u32`, and trailing `u8`. Name follows the PacketParser/source sequence immediately before the assertion-backed `VeDispatchMsg` row at client type `405`; field semantics still need handler evidence. |
| `402` | likely `OP_InviteResponseMsg` | `0x009268ff` serialize, `0x009269a6` deserialize | Same field set as type `401`, followed by an extra `u8` and `string16`. Name follows the PacketParser/source sequence immediately before `VeDispatchMsg`; field semantics still need handler evidence. |
| `403` | likely `OP_InviteTargetResponseMsg` | `0x00926a4d` serialize, `0x00926b02` deserialize | Two `u8` values, two `u32` values, `string16`, five `u32` values, `u8`, and trailing `string16`. Name follows the PacketParser/source sequence immediately before `VeDispatchMsg`; field semantics still need handler evidence. |
| `404` | likely `OP_InspectPlayerRequestMsg` | `0x00926bb7` serialize, `0x00926bf1` deserialize | `string8`, `u32`, `u8`. Name follows the PacketParser/source sequence immediately before `VeDispatchMsg`. This is not the large `WS_InspectPlayer` response struct; the DoF inspect result is nested ClientCmd type `0x1b7` / decimal `439`. |
| `405` | `VeDispatchMsg` | `0x0091ea8c` serialize, `0x0091eb58` deserialize | `u8 method`; methods `1`/`5` and `3`/`8`/`10` carry `string8`, methods `2`/`6`/`7`/`9` carry `u32`, then two `string8` fields and a packed-size raw buffer. Deserializer rejects buffer sizes above `0x8000`. |
| `406` | `OP_DisplayEventMsg` | `0x00929f14` serialize, `0x009463a7` deserialize; constructor `0x0093ce32`; handler `0x0047a810` | Always starts with `string16`. The fresh deserialize object has the pre-mode vector empty, so normal incoming wire then has `u8 mode`, optional two `string8` values plus `u32` when mode is not `0xff`, and a `u8` count of `string8` list entries. A populated object can also serialize an extra pre-mode `u8,string8` branch before `mode`. The handler prints display text, routes onscreen fields to `OnscreenMessage`, and executes the trailing string commands. |
| `407` | `OP_PrePossessionMsg` | `0x0091795c` serialize, `0x00917961` deserialize; handler `0x004a3740` | Stub functions return true; no custom payload fields. The handler snapshots possession/control-state tables and marks possession active. |
| `408` | `OP_PostPossessionMsg` | `0x00917966` serialize, `0x0091796b` deserialize; handler `0x0049df30` | Stub functions return true; no custom payload fields. The handler restores possession/control-state tables, refreshes dependent UI state, and clears possession-active flags. |
| `409` | `VeHouseItemsDetailsMsg` / `OP_HouseItemsDetailsMsg` | `0x00929945` serialize, `0x009475f3` deserialize; handler `0x00473610` | `u32 item_count`, then entries of `u32`, `string16`, `u32`, `u8`, `string16`, `u16`, and `u8`. Deserializer rejects item counts `>= 0x4001`. |
| `410` | compact `OP_HouseItemsList` id list | `0x0092be7d` serialize, `0x0092cf70` deserialize | Handler-backed at `0x004892b0`: `u8 count` followed by that many `u32` IDs, copied into `GameScene + 0x37a4` house-item state. The deserialize decompile rebuilds through a temporary/local container and shows no explicit count cap. Do not label this as PacketParser opcode `410`/`OP_CharTransferValidateRequestMsg`; the stronger client/source-order match for that name is type `369`. v546 XML now sends this compact body for `WS_HouseItemsList` instead of the older full list. |

### Command UI Helper Notes

The shared `EqCmdWidgetPayload` helper used by types `384`, `388`, `392`, `394`, and widget-record cases of type `390` is now resolved enough for deterministic decoding. Its default subrecord vector is allocated with vtable `0x00cd78c8`; vtable slot `0` is `0x009705e4` and slot `1` is `0x00972c1d`. In the widget-payload caller, the subrecord serializer is invoked with flag `1`, so the flag-`0` optional sections are skipped on this path.

For flag `1`, a widget subrecord writes `u32`, five `u16` values, `u8`, two `u32` values, packed `u16`, `u32`, a `u8` count of small records (`u8`, `u8`, `u16`), `u8`, four packed `u16` values, `u8`, `u8`, four `u32` values, six `u8` values, `u32`, `string8`, and `string16`.

The `source/common/emu_oplist.h` command block gives a plausible but weak name sequence for client types `378`-`400`; the client registry rows do not expose assertion-backed names for these classes, and the PacketParser v546 row-id table disagrees with this simple sequence in places. Do not extend this candidate run through `401`-`408`: those rows now have stronger PacketParser/source adjacency to the invite, dispatch, display-event, and possession messages above.

| Client type | Candidate name from EQ command sequence |
| ---: | --- |
| `378` | `OP_EqHearChatCmd` |
| `379` | `OP_EqDisplayTextCmd` |
| `380` | `OP_EqCreateGhostCmd` |
| `381` | `OP_EqCreateWidgetCmd` |
| `382` | `OP_EqCreateSignWidgetCmd` candidate; handler prints arena zone/status list |
| `383` | `OP_EqDestroyGhostCmd` |
| `384` | `OP_EqUpdateGhostCmd` |
| `385` | `OP_EqSetControlGhostCmd` |
| `386` | `OP_EqSetPOVGhostCmd` |
| `387` | `OP_EqHearCombatCmd` |
| `388` | `OP_EqHearSpellCastCmd` |
| `389` | `OP_EqHearSpellInterruptCmd` |
| `390` | `OP_EqHearSpellFizzleCmd` |
| `391` | `OP_EqHearConsiderCmd` |
| `392` | `OP_EqUpdateSubClassesCmd` |
| `393` | `OP_EqCreateListBoxCmd` |
| `394` | `OP_EqSetDebugPathPointsCmd` |
| `395` | `OP_EqCannedEmoteCmd` |
| `396` | `OP_EqStateCmd` |
| `397` | `OP_EqPlaySoundCmd` |
| `398` | `OP_EqPlaySound3DCmd` candidate; handler is empty Arena UI control toggle |
| `399` | `OP_EqPlayVoiceCmd` candidate; handler is empty Arena UI control toggle |
| `400` | `OP_EqHearDrowningCmd` |

## Type ID Encoding

| Address | Ghidra name | Behavior |
| --- | --- | --- |
| `0x0075e625` | `VeStream_WritePackedTypeId_Maybe` | If ID `< 0xff`, writes one byte. Otherwise writes `0xff` then little-endian `uint16`. |
| `0x0075e670` | `VeStream_ReadPackedTypeId_Maybe` | Inverse of packed type ID writer. |
| `0x008fc581` | `VeType_SerializeObjectToStream_Maybe` | Writes type ID and calls message serialize. |
| `0x008fc5c8` | `VeType_DeserializeObjectFromStream_Maybe` | Reads type ID, constructs message, calls deserialize. |
| `0x009859a0` | `VeType_FindById_Maybe` | Type registry lookup by ID. |
| `0x00985f01` | `VeType_Register_Maybe` | Registers `VeType` metadata. |

`FUN_00917970` registers the EQ2 network message types. Direct byte extraction from static initializers found 407 unique IDs in the range `0..410`.

## Stream Helpers

For message field reconstruction, these are the important `VeMemoryStream` / `VeStream` vtable calls:

| Address | Ghidra name | Meaning |
| --- | --- | --- |
| `0x0075ab32` | `VeMemoryStream_ReadRaw_Maybe` | Raw read, usually seen as `stream->vtable[+4](dst, len, 1)`. |
| `0x0075ad7e` | `VeMemoryStream_WriteRaw_Maybe` | Raw write, usually seen as `stream->vtable[+8](src, len, 1)`. |
| `0x00472240` | `VeStream_ReadStringLenN_Maybe` | Reads `N` little-endian length bytes, then string bytes. |
| `0x00472380` | `VeStream_WriteStringLenN_Maybe` | Writes `N` little-endian length bytes, then string bytes. |
| `0x0075abc9` | `VeMemoryStream_GetPosition_Maybe` | Current stream offset. |
| `0x0075adc5` | `VeMemoryStream_Seek_Maybe` | Moves stream offset. |

## Validated Type IDs

Many registered type names are empty in the `VeType` metadata, so names must be inferred from assertion/debug strings in serialize/deserialize methods. These IDs were validated from direct registry extraction plus message method strings:

| Type ID | Inferred message | Deserialize |
| ---: | --- | --- |
| `22` | `VeESInitMsg` | `0x009498a8` |
| `34` | `VePredictionUpdateMsg` | `0x0091e4b8` |
| `51` | `VeDispatchClientCmdMsg` | `0x0091ed2a` |
| `52` | `VeDispatchESMsg` | `0x0091eeac` |
| `55` | `VeUpdateCharacterSheetMsg` | `0x0091f003` |
| `56` | `VeUpdateSpellBookMsg` | `0x0091f0b3` |
| `60` | `VeUpdateRecipeBookMsg` | `0x0091f2c0` |
| `61` | `VeRequestRecipeDetailsMsg` | `0x009368b7` |
| `62` | `VeRecipeDetailsMsg` | `0x0093a2c3` |
| `63` | `VeUpdateSkillsMsg` | `0x0091f3d7` |
| `64` | `VeUpdateSkillsMsg` | `0x0091f499` |
| `104` | `VeQuestJournalSetVisibleMsg` | `0x00936ad1` |
| `117` | `VePurchaseConsignmentResponseMsg` | `0x00936b63` |
| `150` | `VeExamineConsignmentResponseMsg` | `0x00936c86` |
| `155` | `VeKeymapDataMsg` | `0x00921544` |
| `156` | `VeKeymapSaveMsg` | `0x009215e2` |
| `157` | `VeDispatchSpellCmdMsg` | `0x009216b8` |
| `162` | `VeChatRelationshipUpdateMsg` | `0x00944c3e` |
| `179` | `VeDisplayInnVisitScreenMsg` | `0x00947179` |
| `180` | `VeDumpSchedulerMsg` | `0x0094722f` |
| `189` | `VePopulateSkillMapsMsg` | `0x00944d46` |
| `199` | `VeShowRecipeBookMsg` | `0x0092fab2` |
| `201` | `VeKnowledgebaseResponseMsg` | `0x009434fa` |
| `203` | `VeCSTicketInfoMsg` | `0x00944e69` |
| `205` | `VeCSTicketCommentResponseMsg` | `0x00947319` |
| `213` | `VeLsClientBaselogReplyMsg` | `0x00922c4f` |
| `214` | `OP_LsClientCrashlogReplyMsg` | `0x00922cd8` thunk to `0x00922c4f` |
| `215` | `OP_LsClientEq2CrashLogReplyMsg` | `0x00922ce2` thunk to `0x00922c4f` |
| `216` | `OP_LsClientAlertlogReplyMsg` | `0x00922cec` thunk to `0x00922c4f` |
| `217` | `OP_LsClientVerifylogReplyMsg` | `0x00922cf6` thunk to `0x00922c4f` |
| `229` | `VeExamineInfoRequestMsg` | `0x0092323c` |
| `230` | `VeQuickbarInitMsg` | `0x0094505e` |
| `232` | `VeMacroInitMsg` | `0x00948dd0` |
| `252` | `VeGuildBankUpdateMsg` | `0x00923fc2` |
| `314` | `VeUpdateRaidMsg` | `0x00924baf` |
| `315` | `VeUpdateArenaMsg` | `0x00973e1c` |
| `348` | `VeCharTransferStartReplyMsg` | `0x009373bb` |
| `349` | `VeCharTransferRequestMsg` | `0x00937524` |
| `356` | `VeGetCharacterSerializedReplyMsg` | `0x0093767e` |
| `357` | `VeCreateCharFromCBBRequestMsg` | `0x00937838` |

Types `213..217` share the same compressed-log blob body:

```text
packed type id
uint32 compressed_size
uint32 decompressed_size
uint8 compressed_payload[compressed_size]
```

The shared deserializer at `0x00922c4f` enforces
`compressed_size < 0x20001`, reallocates object offset `+0x0c`, and reads the
compressed payload there. The shared serializer at `0x00922bf3` writes zero
compressed size when the payload pointer at `+0x0c` is null; otherwise it writes
object offsets `+0x04` compressed size, `+0x08` decompressed size, and the bytes
at `+0x0c`.

The client login handler at `0x004d5f70` gives stronger names than the legacy
PacketParser table for the log reply family: `crashlog.txt` uses type `214`,
`eq2_crash.log` uses type `215`, `alertlog.txt` uses type `216`, and
`verifylog.txt` uses type `217`. PacketParser v546 labels type `216` as verify
log and type `217` as teleport, so use the client-derived mapping for this
2006 DoF executable until another capture proves otherwise.

## Next Work

See [eq2_2006_client_packet_logger_design.md](eq2_2006_client_packet_logger_design.md)
for a runtime packet logger design that captures clear packet bytes and traces
the client's own stream reads/writes during message serialization.

Use the static registry extractor to regenerate a type map:

```powershell
python scripts/extract_eq2_client_packet_registry.py `
  --exe "E:\Games\Everquest II\EverQuest2.exe" `
  --csv artifacts/eq2_client_packet_registry.csv `
  --json artifacts/eq2_client_packet_registry.json
```

Use the legacy EQ2Emu PacketParser package as a second metadata source for
opcode names and XML wire structs:

```powershell
python scripts\extract_packetparser_metadata.py `
  --packetparser-dir "E:\_EQ2\eq2emu-tools\PacketParser" `
  --client-version 546
```

1. Generate a full `type_id -> factory -> vtable -> serialize -> deserialize` table from the static initializer pattern.
2. Rename each message deserialize function as IDs are confirmed.
3. For each message body, translate `VeMemoryStream_ReadRaw_Maybe` calls into field types by size and offset. Strings use `VeStream_ReadStringLenN_Maybe`.
4. Compare decoded bodies against PacketParser XML structs where an opcode name has a matching struct.
5. Connect high-level handlers by following the listener vtable `+0x14` implementations registered into `ClientNet`.
