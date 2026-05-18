# Legacy Login Scenario Inventory

This inventory maps original login-server behavior to v3 parity work.

## LoginStream Transport

| Scenario | Original Entry Point | Source2 Target | Phase |
| --- | --- | --- | --- |
| UDP client session request/response | `EQStream::ProcessPacket`, `OP_SessionRequest` | `StreamPipeline::handle_session_request` | 2 |
| Duplicate/reconnect session request | `EQStream::ProcessPacket`, `OP_SessionRequest` established-state branch | Stream session state and disconnect behavior | 2 |
| Session disconnect/out-of-session | `OP_SessionDisconnect`, `OP_OutOfSession` | Stream disconnect events and outbound disconnect | 2 |
| Keepalive echo | `OP_KeepAlive` | Keepalive outbound echo | 2 |
| ACK window advancement | `OP_Ack`, `AckPackets` | Sequenced outbound queue ACK tracking | 2 |
| Out-of-order ACK | `OP_OutOfOrderAck` | Retransmit queue marking | 2 |
| Future/past sequenced packet handling | `CompareSequence`, `OutOfOrderpackets`, `SendOutOfOrderAck` | Incoming sequence window handling | 2 |
| Non-sequenced queue | `NonSequencedPush`, `Write` | Immediate and combined non-sequenced outbound writes | 2 |
| Sequenced queue and resend | `SequencedPush`, `CheckResend`, `Write` | Reliable sequenced outbound queue | 2 |
| `OP_Combined` protocol packets | `ProcessPacket`, `OP_Combined` | Decode combined protocol subpackets | 2 |
| Embedded `OP_AppCombined` inside `OP_Packet` | `HandleEmbeddedPacket` | Detect and dispatch embedded app-combined | 2 |
| Fragments | `OP_Fragment`, oversize buffer, `SendPacket` fragmentation | Inbound/outbound fragmentation | 2 |

## Encrypted Login Handshake

| Scenario | Original Entry Point | Source2 Target | Phase |
| --- | --- | --- | --- |
| Session stats request | `OP_ServerKeyRequest` | Stats response event | 3 |
| Key request app packet | `SendKeyRequest` | `OP_WSLoginRequestMsg` serializer | 3 |
| RSA key extraction | `processRSAKey` | RC4 key extraction from correct subpacket | 3 |
| RC4 setup | `Crypto::setRC4Key`, `RC4` | Legacy-compatible RC4 state | 3 |
| Encrypted inbound app data | `ProcessEncryptedData`, `ProcessEncryptedPacket` | Decrypt app opcode/payload | 3 |
| Encrypted app-combined continuation | `OP_AppCombined` loop | Continue after key subpacket | 3 |
| Encrypted outbound app packet preparation | `EQ2Packet::PreparePacket`, `EncryptPacket` | Legacy app header/padding/encryption | 3 |

## Account Login Policy

| Scenario | Original Entry Point | Source2 Target | Phase |
| --- | --- | --- | --- |
| Classic login request parse | `Client::Process`, `OP_LoginRequestMsg`, `LS_LoginRequest` version 1 | Legacy login request parser | 4 |
| Newer login request parse | fallback `LS_LoginRequest` version 1208 | Version-aware parser | 4 |
| Unsupported version | `SendLoginDenied`, `SendLoginDeniedBadVersion` | Matching rejection payloads | 4 |
| Account load/auth/create | `LoginDatabase::LoadAccount` | Account repository/auth service | 4 |
| Duplicate login session | `ClientList::FindByLSID`, `FatalError` | Duplicate session handling | 4 |
| Account IP update | `UpdateAccountIPAddress` | DB update after accepted login | 4 |
| Client data version update | `UpdateAccountClientDataVersion` | DB update after accepted login | 4 |
| Banned account/IP behavior | `login_bannedips`/account checks | Auth rejection policy | 4 |
| Login accepted/denied payloads | `SendLoginAccepted`, `SendLoginDenied` | Exact response serializer | 4 |

## World Registration And World List

| Scenario | Original Entry Point | Source2 Target | Phase |
| --- | --- | --- | --- |
| World TCP registration | `LWorld::SetupWorld` | World registration parser/service | 5 |
| World account auth | `CheckServerAccount`, `IsServerAccountDisabled` | DB-backed world account auth | 5 |
| Bad world version | `ERROR_BADVERSION` path | Registration rejection | 5 |
| Ghost world cleanup | `KickGhost`, `KickGhostIP` | Duplicate world handling | 5 |
| World status/list update | `UpdateWorldList`, `SendWorldChanged` | Registered world cache and update packets | 5 |
| Full world list packet | `MakeServerListPacket` | Legacy world-list serializer | 5 |
| World stats persistence | `UpdateWorldServerStats`, `RemoveOldWorldServerStats` | DB maintenance jobs | 5 |
| Zone update handling | `SetServerZoneDescriptions`, `RequestServerUpdates` | World zone update repository/flow | 5 |
| Equipment appearance updates | `SetServerEquipmentAppearances`, `RequestServerEquipUpdates` | Login equipment update flow | 5 |

## Character Lifecycle

| Scenario | Original Entry Point | Source2 Target | Phase |
| --- | --- | --- | --- |
| Character list request | `OP_AllCharactersDescRequestMsg`, `SendCharList` | Character-list service | 6 |
| Character list serialization | `LS_CharSelectList`, `CharSelectProfile` | Versioned character serializers | 6 |
| Character loading | `LoadCharacters`, `CheckCharacterTimeStamps` | DB-backed character repository | 6 |
| Create character request | `OP_CreateCharacterRequestMsg` | Create parser and world forward | 6 |
| Create approval/rejection | `CharacterApproved`, `CharacterRejected` | Response handling and DB save | 6 |
| Delete character request | `OP_DeleteCharacterRequestMsg` | Delete parser and world forward | 6 |
| Pending delete verification | `VerifyDelete`, `DeleteCharacter` | DB verification/update | 6 |
| Play character request | `OP_PlayCharacterRequestMsg` | Play parser and handoff service | 6 |
| World play response | `WorldResponse`, `LS_PlayResponse` | Client play response serializer | 6 |

## Client Logs, Admin, Web, And Maintenance

| Scenario | Original Entry Point | Source2 Target | Phase |
| --- | --- | --- | --- |
| Client crash logs | `OP_LsClientCrashlogReplyMsg`, `SaveErrorsToDB` | Client log parser/storage | 7 |
| Client verify logs | `OP_LsClientVerifylogReplyMsg` | Client log parser/storage | 7 |
| Client alert logs | `OP_LsClientAlertlogReplyMsg` | Client log parser/storage | 7 |
| Client base logs | `OP_LsClientBaselogReplyMsg` | Client log parser/storage | 7 |
| Login config load | `NetConnection::ReadLoginConfig` | Typed config parity | 7 |
| Console commands | `net.cpp` console loop | Operator commands or replacement docs | 7 |
| Web status | `Web/LoginWeb.cpp` | Status endpoint or replacement docs | 7 |
| Periodic DB maintenance | `net.cpp` loop and `LoginDatabase` maintenance methods | Maintenance scheduler | 7 |

