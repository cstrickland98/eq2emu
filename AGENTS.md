# AGENTS.md

## Project Context

This repository is an MMO server emulator. Treat correctness, stability, and maintainability as higher priority than speed of edits.

The codebase may include:
- C++ server logic
- Lua content scripts for quests, NPCs, spawns, items, and encounters
- SQL/database schema and content data
- Packet/opcode/serialization systems
- Build scripts, tooling, and test utilities

Do not assume game protocol behavior. Search existing implementations first.

## Repository Layout

- `source/common/` contains shared packet, stream, database, logging, crypto, config, XML struct, and utility code used by both servers.
- `source/LoginServer/` contains login/account/session server code and login-specific packet/opcode handling.
- `source/WorldServer/` contains world simulation, clients, Lua integration, quests, combat, items, spells, rules, commands, and world database code.
- `source/WorldServer/Zone/` contains zone maps, movement, pathing, regions, raycast, and navigation support.
- `server/` is the runtime directory. It contains config examples, local configs, packet struct XML files, logs, copied binaries/DLLs, and content script directories.
- `server/ItemScripts`, `server/PlayerScripts`, `server/Quests`, `server/RegionScripts`, `server/SpawnScripts`, `server/Spells`, and `server/ZoneScripts` may be junctions to a sibling `eq2emu-content` checkout. Resolve the real path before editing content scripts, and do not assume those files are owned by this repo.
- `docs/lua_api.md` and `docs/lua_functions/` document available Lua functions and should be checked before adding or changing Lua script calls.
- `docs/database/login/`, `docs/database/world/`, `docs/logindb_schema.md`, and `docs/worlddb_schema.md` document database tables and should be checked before SQL or DB-loader changes.
- `docs/code/` contains code reference notes that can help orient subsystem ownership, but source files remain authoritative.
- `scripts/` contains Windows build, run, database setup, and dependency helper scripts.
- `build/`, `out/`, and `artifacts/` are local/generated output areas. Do not edit or rely on generated contents there unless the task explicitly targets them.

## Core Rules

- Prefer small, focused changes.
- Preserve existing behavior unless the task explicitly asks to change it.
- Never rewrite large systems unnecessarily.
- Never modify generated, vendored, or third-party code unless explicitly asked.
- Do not guess packet layouts, opcodes, struct fields, serialization order, or binary formats.
- Do not silently remove logging, assertions, validation, or safety checks.
- Do not introduce global state unless there is already an established project pattern.
- Favor readable, debuggable code over clever code.

## Before Making Changes

Before editing code:
1. Search the repo for existing patterns.
2. Identify the owning subsystem.
3. Read nearby code before changing it.
4. Check whether tests, scripts, or fixtures already exist.
5. Make a brief plan for non-trivial changes.

Useful search commands:
- `rg "keyword"`
- `rg "class Name|struct Name|functionName"`
- `rg "opcode|packet|serialize|deserialize|lua|quest"`

## Build and Test Expectations

After code changes, run the narrowest relevant validation first.

Prefer:
- Build only the affected target if possible.
- Run focused tests before full test suites.
- Run Lua/script validation for content-script changes.
- Run packet/serialization tests for protocol changes.
- Run database migration or SQL validation for schema/content changes.

Known local validation entry points:
- Windows build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-windows.ps1`
- Clean Windows build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build-windows.ps1 -Clean`
- Prepare runtime files: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-windows.ps1`
- Run both servers locally after a successful build: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\run-windows.ps1 -Mode both -CheckPorts`
- Direct CMake preset build: `cmake --build --preset windows-msvc-vcpkg --config Debug`

Windows builds use the `windows-msvc-vcpkg` CMake preset and require `VCPKG_ROOT`, Visual Studio C++ tools, CMake, Ninja, and vcpkg. The CMake/vcpkg path is additive; preserve the legacy Linux entry points unless explicitly asked to change them.

This checkout may not have focused automated tests for every subsystem. If no relevant test or fixture exists, say so and use the narrowest build, syntax check, SQL validation, or runtime smoke check available.

If a command fails, report:
- the command
- the failure
- the likely cause
- whether the failure appears related to the change

Do not claim tests passed unless they were actually run.

## C++ Guidelines

- Follow existing project style.
- Prefer explicit types where clarity matters.
- Avoid unnecessary heap allocation.
- Avoid changing public interfaces unless required.
- Be careful with object lifetimes, ownership, references, and pointers.
- Check bounds before reading packet buffers or arrays.
- Validate client-provided data.
- Avoid undefined behavior.
- Keep hot-path server code efficient.

For packet code:
- Preserve field order.
- Preserve integer sizes and signedness.
- Preserve endian assumptions.
- Add tests or fixtures when changing serialization/deserialization.
- Search for matching read/write handlers before editing.

## Lua Content Guidelines

Lua scripts should be:
- deterministic
- easy to inspect
- consistent with existing quest/NPC script patterns
- defensive against missing player, NPC, item, quest, or zone state

When creating or editing quest scripts:
- Search for similar quests first.
- Reuse existing helper functions and conventions.
- Keep quest step names, item ids, NPC ids, and location assumptions explicit.
- Do not invent IDs unless the task provides them or an existing source confirms them.
- Add comments only where behavior is non-obvious.

## Database and Content Data Guidelines

- Do not make destructive schema/data changes unless explicitly requested.
- Prefer migrations over ad-hoc SQL changes when the project uses migrations.
- Preserve existing IDs and relationships.
- Be cautious with spawn tables, loot tables, quests, NPCs, factions, and zone data.
- For SQL changes, consider indexes, constraints, and rollback behavior.
- Treat `docs/database/` and schema documentation as references, not migrations.
- Do not edit local database files under `build/mysql-data` unless explicitly asked.

## Performance Expectations

This is a server emulator. Avoid changes that add unnecessary work to:
- packet handling
- zone loops
- spawn/entity updates
- combat loops
- pathing
- database polling
- high-frequency timers

Before adding expensive logic, look for caching, batching, indexing, or existing subsystem support.

## Reverse Engineering / Protocol Safety

This repo may contain protocol knowledge derived from legitimate reverse engineering.

When working in protocol areas:
- Do not fabricate missing protocol details.
- Mark uncertain findings clearly.
- Prefer adding TODOs over pretending uncertain behavior is known.
- Keep protocol changes isolated and testable.
- Compare against nearby client-version handling if available.

## Agent Workflow

For each task:
1. Restate the goal briefly.
2. Inspect relevant files.
3. Make the smallest safe change.
4. Run relevant validation.
5. Summarize what changed and what was tested.

When unsure:
- Search more.
- Prefer asking for clarification over guessing.
- If blocked, explain the blocker and suggest the next concrete step.

## Files and Directories to Treat Carefully

Do not casually modify:
- packet/opcode definitions, including `source/common/op_codes.h`, `source/common/emu_opcodes.*`, `source/common/emu_oplist.h`, `source/common/login_oplist.h`, and `source/LoginServer/login_opcodes.h`
- packet struct definitions, including `server/CommonStructs.xml`, `server/EQ2_Structs.xml`, `server/ItemStructs.xml`, `server/LoginStructs.xml`, `server/SpawnStructs.xml`, and `server/WorldStructs.xml`
- serialization/deserialization code, including `source/common/PacketStruct.*`, `source/common/ConfigReader.*`, `source/common/EQPacket.*`, and `source/common/EQStream.*`
- database migrations
- generated files
- vendored dependencies
- large content data dumps
- build system files
- authentication/account logic
- networking/session code
- local runtime configs such as `server/login_db.ini`, `server/world_db.ini`, and `server/server_config.json`; prefer changing `*.example` files or docs unless the task is explicitly local setup
- runtime logs, copied binaries, copied DLLs, and database files under `server/`, `build/`, `out/`, or `artifacts/`

## Commit / PR Style

Summaries should include:
- what changed
- why it changed
- affected subsystem
- tests or validation run

Example:

`Fix quest step completion for NPC hail interaction`

- Updated Lua quest script to match existing hail-completion pattern.
- Preserved existing quest IDs and step ordering.
- Ran Lua syntax validation.
