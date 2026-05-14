# EQ2Emu Project Knowledge

Purpose: concise AI-agent reference for navigating and changing this project.

## Project Identity

- EQ2Emu is a C++17 EverQuest II emulator server.
- Runtime is split into two executables:
  - `login`: login/account/server-list service.
  - `eq2world`: world, zone, character, gameplay, Lua, database, and client service.
- Primary runtime directory is `server/`; binaries are copied there and expect config/XML/content files in that working directory.
- Database backend is MariaDB. README states MySQL is not supported.

## Top-Level Layout

- `source/common/`: shared networking, packet, database, logging, XML/config, crypto, timers, mutexes, utilities.
- `source/LoginServer/`: login executable, login-client packet handling, world-server TCP links, login DB access.
- `source/WorldServer/`: main game server, zones, clients, spawns/entities, gameplay systems, Lua, DB loaders, web/peer APIs.
- `server/`: runtime templates and protocol structure XMLs:
  - `CommonStructs.xml`, `LoginStructs.xml`, `WorldStructs.xml`, `SpawnStructs.xml`, `ItemStructs.xml`, `EQ2_Structs.xml`.
  - `login_db.ini.example`, `world_db.ini.example`, `server_config.json.example`, `log_config.xml.example`.
- `docs/`: generated/reference docs for DB schema, Lua API/functions, slash commands, data types, and code pages.
- `linux_compile.sh`: Linux bootstrap script that installs deps, builds Recast/fmt/source, and copies content/map repos into `server/`.

## Build/Dependency Notes

- Login build: `source/LoginServer/makefile`, outputs `login`.
- World build: `source/WorldServer/makefile`, outputs `eq2world`.
- Main dependencies visible in makefiles/scripts:
  - MariaDB client library, Boost, OpenSSL/libcrypto, zlib, pthread, readline.
  - Lua 5.4 for scripting.
  - Recast/Detour for navigation/pathing.
  - fmt headers, GLM, SDL/OpenGL/GLFW packages from installer context.
- World makefile expects include/lib paths under `/eq2emu/...`; `linux_compile.sh` rewrites those to the configured install root.

## Runtime Configuration

- `server/server_config.json` controls login/world network settings and optional web endpoints.
  - `LoginServer`: login address/port, world name/address/internal address/world port, world account/password.
  - `WorldServer`: default admin status and world web server settings.
  - `LoginConfig`: login server mode, server port, account creation, login web server settings.
- `server/login_db.ini` connects login server to login DB, normally `eq2ls`.
- `server/world_db.ini` connects world server to world DB, normally `eq2emu`.
- `server/log_config.xml` controls log categories/levels.
- XML structs are loaded at startup and drive client-version-aware packet serialization.

## Shared Architecture

- `EQStreamFactory` and `EQStream` in `source/common/` implement EQ network stream/session behavior, sequencing, acks, packet queues, compression/encryption, timeout checks, and stream-to-client handoff.
- `PacketStruct` plus XML struct files provide data-driven packet layout handling.
- `OpcodeManager` loads opcode mappings by client version from DB and maps symbolic op names to numeric values.
- `Database`/`DatabaseNew`/`DatabaseResult` are shared DB layers. World still uses old and new DB paths side by side.
- Global singletons/managers are common. Many major systems are file-scope globals in `net.cpp` and related modules.

## Login Server Flow

Entry point: `source/LoginServer/net.cpp::main`.

Startup:
- Start logging and parse log config.
- Read login config from `server_config.json`.
- Start optional login web server.
- Load `CommonStructs.xml` and `LoginStructs.xml`.
- Initialize `world_list`.
- Open login UDP listener with `eqsf.Open(net.GetPort())`.

Main loop:
- Pop new `EQStream`s from `eqsf`, wrap each in `LoginServer::Client`, add to `client_list`.
- Every 5 seconds, check stream timeouts.
- Periodically update world stats and clean old stats/bug reports.
- Process `client_list` and `world_list`.

Important login classes:
- `Client` (`source/LoginServer/client.*`): handles login client opcodes: login request, world list, character list/create/delete/play.
- `LWorld` (`source/LoginServer/LWorld.*`): represents a TCP-connected world server or login uplink. Handles `ServerOP_*` interserver packets.
- `LWorldList`: accepts/owns world TCP connections, broadcasts world-list/status updates, routes encapsulated packets.
- `LoginDatabase`: login/account/world registration DB access.

Login/world interaction:
- Login accepts world server TCP connections through `LWorldList`.
- World authenticates with `ServerOP_LSInfo`.
- Login routes character create/play and user-to-world requests to world via `ServerOP_*` packets.

## World Server Flow

Entry point: `source/WorldServer/net.cpp::main`.

Startup:
- Mark `net.is_primary = true`.
- Start logging and parse log config.
- Initialize old DB and new DB connections.
- Install signal handlers.
- Read world/login settings from `server_config.json`.
- Load opcode versions/mappings from DB into `EQOpcodeManager`.
- Load packet structs: `CommonStructs.xml`, `WorldStructs.xml`, `SpawnStructs.xml`, `ItemStructs.xml`.
- Initialize `world` and optional web server.
- Load system/game data from DB:
  - rules, EQ2 time, items, spells, traits, quests, collections, merchants.
  - recipes/tradeskills, AAs, titles, languages, chat channels.
  - Lua spawn/zone/player script metadata.
  - housing, heroic opportunities, race types, transmuting, chest traps, NPC spells.
  - guilds, player houses, broker data, special zones.
- Open world UDP listener on configured world port.
- Start peer polling and optional console thread.

Main loop:
- Pop new client `EQStream`s from `eqsf`.
- Reject banned IPs if configured.
- Wrap active streams in `WorldServer::Client` and add to `client_list`.
- Process `world`, `client_list`, and `loginserver`.
- Periodically check stream timeouts and ping DB connections.
- Auto-reconnect to login server when disconnected and eligible.

Shutdown:
- Stop peer HTTPS polling.
- Shut down zones.
- Delete Lua interface and timeout timer.
- Close network stream factory.
- Delete opcode managers and stop logging.

## World Core Systems

- `World` (`source/WorldServer/World.*`):
  - Global world state and timed processing.
  - Caches scripts, merchants, houses, maps/region maps, voiceovers, NPC spells, statistics, world time.
  - Owns web handlers for world status, clients, zones, peer management, broker, groups, guilds, housing, chat, and reloads.
- `ZoneList` (`World.h`/`World.cpp`):
  - Owns active `ZoneServer` instances and cross-zone client lookup.
  - Broadcasts chat/announcements, manages zone shutdown/depop/repop, who queries, client maps, vitality, mail/reload helpers.
- `ZoneServer` (`source/WorldServer/zoneserver.*`):
  - Core zone simulation owner.
  - Owns clients, spawns, spawn locations, faction lists, transporters, weather, pathing/movement, respawns, loot, combat process hooks, tradeskill manager, spell process.
  - Has multiple timers and thread entry functions (`ZoneLoop`, `SpawnLoop`, initial spawn send).
- `Client` (`source/WorldServer/client.*`):
  - Per-player connection/session object.
  - Handles packet processing, zoning, spawn updates, quests, inventory/bank/mail, merchants, loot, guild/group UI, recipes, achievements, housing, transports, dialogs, waypoints, save state.
- `Player`/`Entity`/`Spawn` family:
  - `Spawn` is the base world object with position/appearance/update state.
  - `Entity` adds combat/stat/spell capabilities.
  - `Player`, `NPC`, `Bot`, `Object`, `Widget`, `Sign`, `GroundSpawn` specialize spawn behavior.
- `WorldDatabase`:
  - Large DB facade for loading and saving world/game state. Many systems add `*DB.cpp` files beside their modules.

## Gameplay Subsystems

Common subsystem directories under `source/WorldServer/`:

- `Achievements/`: achievement data and DB.
- `AltAdvancement/`: AA data and DB loading.
- `Bots/`: bot entity, bot brain, bot DB, bot commands.
- `Broker/`: market/broker data.
- `Chat/`: channels and chat DB.
- `Collections/`: collection data/reward flow.
- `Commands/`: slash/console/remote command parsing and dispatch.
- `Guilds/`: guild state and DB persistence.
- `HeroicOp/`: heroic opportunity data, packets, DB.
- `Housing/`: player house/housing behavior.
- `Items/`: item definitions, packets/struct variants, item DB, loot DB.
- `Recipes/` and `Tradeskills/`: crafting data/process/packets.
- `Rules/`: runtime rule manager and DB-backed rules.
- `Traits/`: trait data.
- `Zone/`: maps, region maps, pathfinders, raycast mesh, movement manager.

## Lua Content System

- Main bridge: `source/WorldServer/LuaInterface.*`.
- Function implementations: `source/WorldServer/LuaFunctions.*`.
- Lua state is created with `luaL_newstate`; functions are registered in `LuaInterface::RegisterFunctions`.
- Registered Lua API is broad: spawn stats, combat, spells, items, quests, movement, conversations, loot, zones, weather/time, rules, houses/widgets, mail, titles, etc.
- Runtime script folders expected in `server/`:
  - `ItemScripts/`, `Quests/`, `RegionScripts/`, `SpawnScripts/`, `Spells/`, `ZoneScripts/`, `PlayerScripts/`.
- Docs for Lua functions are in `docs/lua_functions/` and aggregate API docs in `docs/lua_api.md`.

## Data/Packet Model

- Client protocol support is data-driven:
  - DB supplies opcode versions and opcode numbers.
  - XML struct files supply packet layouts.
  - C++ packet handlers use `PacketStruct`, `EQ2Packet`, `ServerPacket`, and opcode names.
- Login client packet handlers live mostly in `source/LoginServer/client.cpp`.
- World client packet handlers live mostly in `source/WorldServer/client.cpp` and supporting `*Packets.cpp` subsystem files.
- Interserver packets use `ServerOP_*` opcodes and `ServerPacket`.

## Main Interaction Map

```text
EQ2 client
  -> login UDP (`login`, EQStreamFactory)
  -> LoginServer::Client authenticates account and lists worlds/chars
  -> LoginServer::LWorld routes create/play requests to world over TCP
  -> WorldServer::LoginServer receives ServerOP_UsertoWorldReq / char ops
  -> eq2world UDP accepts client stream
  -> WorldServer::Client owns session
  -> ZoneList selects/creates ZoneServer
  -> ZoneServer simulates spawns/combat/movement/scripts
  -> WorldDatabase persists and loads game state
  -> LuaInterface invokes content scripts for NPCs, quests, spells, zones
```

## Where To Look First

- Startup/config/network listener: `source/LoginServer/net.cpp`, `source/WorldServer/net.cpp`.
- Login protocol/account/character list: `source/LoginServer/client.cpp`, `LoginDatabase.*`.
- World-login TCP protocol: `source/WorldServer/LoginServer.*`, `source/LoginServer/LWorld.*`, `source/common/servertalk.h`.
- Client gameplay packets: `source/WorldServer/client.cpp`, `ClientPacketFunctions.*`, subsystem `*Packets.cpp`.
- Zone simulation/spawns/combat proximity: `source/WorldServer/zoneserver.*`.
- Global world data/cache/timers/web handlers: `source/WorldServer/World.*`.
- DB load/save behavior: `source/WorldServer/WorldDatabase.*` and subsystem `*DB.cpp`.
- Lua API or script behavior: `source/WorldServer/LuaInterface.*`, `LuaFunctions.*`, `docs/lua_functions/`.
- Network stream/session issues: `source/common/EQStream.*`, `EQStreamFactory.*`, `EQPacket.*`, `opcodemgr.*`, `PacketStruct.*`.
- Command behavior: `source/WorldServer/Commands/Commands.*`, `CommandsDB.cpp`, `docs/slash_commands/`.

## Agent Working Notes

- Prefer `rg` for locating handlers by opcode/function name.
- This codebase uses many globals and manager singletons; check existing ownership/lifetime before moving logic.
- Be careful around timers, mutex-protected containers, and zone/client lists; many systems are processed from long-running loops and/or auxiliary threads.
- Packet changes usually require checking opcode version, XML struct names, packet builder code, and client version behavior.
- DB changes likely need schema/data updates outside this repo unless docs/schema files are enough for the task.
- Runtime content scripts/maps are separate from this source repo in normal Linux install (`eq2emu-content`, `eq2emu-maps`).
- Existing docs are extensive but appear generated/reference-style; verify behavior in source before relying on docs for implementation.
