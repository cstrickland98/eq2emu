# Phase 1 Runtime Inventory

Status: complete.

## Known Executables

| Executable | Entry point | Legacy output |
| --- | --- | --- |
| Login server | `source/LoginServer/net.cpp` | `login` |
| World server | `source/WorldServer/net.cpp` | `eq2world` |

## Known Configuration Inputs

| Config | Current reference | Purpose |
| --- | --- | --- |
| `server/server_config.json.example` | Runtime example config | Login/world address, ports, web config, login mode |
| `login_db.ini` | `source/common/Common_Defines.h`, `source/common/DatabaseNew.cpp`, `source/common/dbcore.h` | Login database config |
| `world_db.ini` | `source/common/Common_Defines.h`, `source/common/DatabaseNew.cpp`, `source/common/dbcore.h` | World database config |

## Known Ports And Addresses

Initial defaults and config keys found so far:

| Area | Key or field | Current reference |
| --- | --- | --- |
| Login server listener | `LoginConfig.ServerPort` | `server/server_config.json.example` |
| World-to-login connection | `LoginServer.loginserver`, `LoginServer.loginport` | `server/server_config.json.example`, `source/WorldServer/net.cpp` |
| World server advertised port | `LoginServer.worldport` | `server/server_config.json.example`, `source/WorldServer/net.cpp` |
| Web login port | `LoginConfig.webloginport` | `source/LoginServer/net.cpp` |
| Web world port | `WorldServer.webport` | `source/WorldServer/net.cpp` |

## Startup Flow

### Login Server

1. Trace `source/LoginServer/net.cpp::main`.
2. Install `SIGINT` handler, which sets global `RunLoops = false`.
3. Start logging with `LogStart()` and parse `log_config.xml` through `LogParseConfigs()`.
4. Print welcome header.
5. Parse `server_config.json` through `NetConnection::ReadLoginConfig`.
6. Initialize the login database through `LoginDatabase::Init`.
7. Load opcode versions and opcode maps from the DB.
8. Start optional login web server if web address and port are configured.
9. Load `CommonStructs.xml` and `LoginStructs.xml`.
10. Initialize `world_list`.
11. Open `EQStreamFactory` on `LoginConfig.ServerPort`.
12. Main loop:
    - Update timer state.
    - Pop new `EQStream` instances from `EQStreamFactory`.
    - Create `Client` objects for new login clients.
    - Check stream timeouts.
    - Periodically update world stats and cleanup login DB data.
    - Process login clients.
    - Process connected world servers.
13. Shutdown:
    - Close `EQStreamFactory`.
    - Shutdown `world_list`.
    - Return.

### World Server

1. Trace `source/WorldServer/net.cpp::main`.
2. Set `net.is_primary = true`.
3. Start logging and parse `log_config.xml`.
4. Initialize legacy DB and `DatabaseNew`.
5. Install signal handlers for `SIGINT`, `SIGSEGV`, `SIGILL`, and ignore `SIGPIPE` on non-Windows.
6. Parse `server_config.json` and command-line overrides through `NetConnection::ReadLoginINI`.
7. Load opcode versions and opcode maps from the DB.
8. Load `CommonStructs.xml`, `WorldStructs.xml`, `SpawnStructs.xml`, and `ItemStructs.xml`.
9. Initialize `world`, including optional web server setup.
10. Load system and gameplay data:
    - Login server variables.
    - Items and spells either synchronously or through detached load threads.
    - Recipes, tradeskills, alternate advancements, titles, languages, channels.
    - Spawn/zone/player script data.
    - House zones, heroic opportunities, race types, transmuting, chest traps, NPC spells.
    - Guilds and player houses.
11. Open `EQStreamFactory` on configured world port.
12. Mark world loaded.
13. Load broker data.
14. Start peer polling thread.
15. Enter main loop:
    - Update timer and frame time.
    - Pop new `EQStream` instances from `EQStreamFactory`.
    - Check banned IPs.
    - Create `Client` instances or hold them in a temporary waiting map.
    - Process `world`.
    - Process master `client_list`.
    - Process `loginserver`.
    - Check stream timeouts.
    - Ping legacy, new, and async DB connections.
    - Auto-reconnect to login server when needed.
16. Shutdown:
    - Stop peer polling.
    - Shutdown zones.
    - Delete Lua interface.
    - Delete timeout timer.
    - Close `EQStreamFactory`.
    - Delete opcode managers.
    - Stop logging.

## Global State To Inventory

Inventory:

- `NetConnection net`.
- `World world`.
- `EQStreamFactory eqsf`.
- `LoginServer loginserver`.
- `LuaInterface* lua_interface`.
- `ClientList client_list`.
- `ZoneList zone_list`.
- `WorldDatabase database`.
- `ConfigReader configReader`.
- `RuleManager rule_manager`.
- Master data lists for spells, items, skills, traits, factions, titles, languages, achievements, guilds, broker, and chest traps.
- Global opcode state: `EQOpcodeManager` and `EQOpcodeVersions`.

## Critical Runtime Flows

### Login Authentication

1. Login client connects to login `EQStreamFactory`.
2. Login main loop creates `source/LoginServer/Client`.
3. `Client::Process` pops application packets.
4. `OP_LoginRequestMsg` is parsed using `LS_LoginRequest`.
5. Client version is resolved against `EQOpcodeManager`.
6. `LoginDatabase::LoadAccount` checks or creates account data.
7. Login response is sent.
8. Client requests world and character data.
9. Login server loads characters and sends world/character list packets.

### World Registration

1. World server reads login server address and credentials from `server_config.json`.
2. World connects to login via `LoginServer::Connect`.
3. World sends `ServerOP_LSInfo` and `ServerOP_LSStatus`.
4. Login `LWorld::Process` requires `ServerOP_LSInfo` before other world packets.
5. Login validates protocol version, server version, world account, password, and disabled/banned state.
6. Login marks the world authenticated and tracks world status.

### Play Character

1. Login client sends `OP_PlayCharacterRequestMsg`.
2. Login server identifies character and target world server.
3. Login sends `ServerOP_UsertoWorldReq` to that world.
4. World `LoginServer::Process` receives the request.
5. World loads character zoning details through `WorldDatabase::loadCharacterFromLogin`.
6. World determines admission and creates a `ZoneAuthRequest`.
7. World returns `ServerOP_UsertoWorldResp` with access key, host, and port.
8. Login forwards play response to client.

### World Client Entry

1. Client connects to world server.
2. Client sends `OP_LoginByNumRequestMsg`.
3. World parses version, account ID, and access key.
4. `Client::HandleNewLogin` validates the key in `zone_auth`.
5. World loads character state and zone state.
6. Client is removed from master `client_list`.
7. Client is added to the owning zone.
8. Zone `new_client` Lua script is invoked if configured.

## Source2 Implications

- `apps/login_server` and `apps/world_server` should become thin composition roots.
- Runtime startup should be decomposed into config, logging, DB, network, protocol, and application services.
- Shutdown must become explicit and RAII-driven.
- Global mutable state should be replaced by owned services passed through constructors or runtime context objects.
- Login/world/zone handoff should become an explicit interface instead of shared globals plus server packets.
- The source2 app layer should not contain data loading logic beyond sequencing services.

## Phase 1 Runtime Exit Result

Runtime inventory is sufficient for phase 2 planning. The most important gaps to validate during review are whether source2 keeps login/world in separate executables and whether zone ownership is one executor per zone or grouped by shard.
