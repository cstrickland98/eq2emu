# EQ2Emu vs AzerothCore Architecture Comparison

Date: 2026-05-14

Compared repositories:

- EQ2Emu: `E:\_EQ2\eq2emu`
- AzerothCore/WoW: `E:\_WOW\azerothcore-wotlk`

## Executive Summary

EQ2Emu and AzerothCore solve the same broad problem: they are C++ MMORPG server emulators with separate authentication/login and world/gameplay processes, database-backed game state, custom packet protocols, command systems, scripting/content systems, and a large amount of domain-specific gameplay code.

The architectural maturity difference is substantial. EQ2Emu currently looks like a functional but young emulator codebase: two hand-built executables, many global managers, a large gameplay surface concentrated in a small number of world-server classes, runtime XML packet definitions, Lua content scripting, and generated/reference documentation. AzerothCore is a mature platform: it has a structured CMake build graph, explicit library boundaries, bundled dependencies, CI, tests, database migrations, database updater tooling, module/plugin support, script loader generation, configuration policy, logging infrastructure, Docker/dev tooling, and a long-lived contribution workflow.

EQ2Emu can be made more like AzerothCore, but the useful target is not copying WoW gameplay architecture wholesale. The games, protocols, packet layout model, content format, and scripting style differ too much. The high-value opportunity is to adopt AzerothCore's platform architecture patterns: build system, dependency handling, database migration/updater workflow, test harness, modular extension points, config/logging policy, CI, tools layout, and stricter subsystem boundaries.

Direct code extraction from AzerothCore should be treated carefully. AzerothCore is GPL v2 licensed at the repository level, while EQ2Emu is GPL v3 licensed at the repository level. Some individual AzerothCore files say "GPL v2 or later", but the safe path is to copy architectural ideas and reimplement them for EQ2Emu unless license compatibility and contributor provenance are verified.

## Repository Scale and Shape

| Area | EQ2Emu | AzerothCore |
| --- | --- | --- |
| Main source root | `source/` | `src/` |
| Main executables | `login`, `eq2world` | `authserver`, `worldserver` |
| Build system | Hand-written makefiles and Visual Studio project files | Top-level CMake project with options, macros, generated targets, install rules, tests, modules |
| C++ source/header files | About 256 under `source/` | About 1,683 under `src/`, about 2,954 including `modules/` |
| Database material in repo | Schema/reference docs, no SQL files found in `source/docs/server` scan | Over 7,000 SQL files under `data/sql` |
| Tests | No active test tree found | Google Test target under `src/test` |
| CI/project workflow | No `.github` tree found | GitHub workflows for builds, tools, SQL style, codestyle, Docker, macOS, Windows, modules |
| Extension mechanism | Lua content scripts and some subsystem directories | C++ script system, dynamic/static scripts, external modules under `modules/` |
| Runtime config | `server/*.ini.example`, `server_config.json.example`, `log_config.xml.example` | `conf/dist`, app `.conf.dist`, config policy, config merger tooling |
| Dependencies | System packages plus installer/build script paths | Bundled `deps/` plus CMake discovery and options |

## Similarities

Both projects have the same basic emulator split:

- A login/auth service handles account login and server/realm selection.
- A world service handles connected players and gameplay.
- A shared/common layer contains networking, cryptography, packets, logging, timers, utilities, and database access.
- The world server owns global state, sessions, zones/maps, entities, commands, combat, items, quests, guilds, chat, loot, and persistence.
- Both rely heavily on a relational database for persistent account, character, and world/content data.
- Both need data-driven behavior because retail client protocol and content behavior are too large to hard-code cleanly.
- Both have pathing/navigation dependencies related to Recast/Detour.
- Both use global singleton-style managers in important runtime paths, although AzerothCore has more defined library boundaries around them.

The most important similarity is the runtime topology:

```text
Client
  -> login/auth server
  -> world/game server
  -> session object
  -> world/map/zone owner
  -> entity/gameplay systems
  -> database and content/script systems
```

This means many AzerothCore maturity patterns are conceptually applicable to EQ2Emu even when the exact code is not.

## Major Differences

### 1. Build Architecture

EQ2Emu:

- Uses `source/LoginServer/makefile` and `source/WorldServer/makefile`.
- Uses fixed include/library paths that are rewritten or assumed by `linux_compile.sh`.
- Has Visual Studio project artifacts for the login server.
- Produces two executables directly from broad source file globs or explicit object lists.
- Does not expose a top-level build graph for common, login, world, database, scripts, tests, tools, and modules.

AzerothCore:

- Has a root `CMakeLists.txt` that defines the project, policies, options, dependency discovery, out-of-source build behavior, install behavior, and generated revision metadata.
- Splits build targets into `common`, `database`, `shared`, `game`, `scripts`, `modules`, `apps`, `tools`, and `test`.
- Supports selective app builds (`auth-only`, `world-only`, all/none), script linkage modes, module modes, tools builds, PCH, and tests.
- Uses CMake helper macros to collect sources, include directories, install config, and print build graphs.

Impact:

EQ2Emu's build is simpler but fragile. AzerothCore's build system is part of its stability because dependency boundaries and optional components are visible and testable.

### 2. Library and Dependency Boundaries

EQ2Emu:

- `source/common` is shared by both executables.
- `source/WorldServer` contains most gameplay systems and many cross-system globals.
- Database logic is split between common DB classes, `WorldDatabase`, `LoginDatabase`, and subsystem `*DB.cpp` files, but not through independent build libraries.
- The world server is compiled as one broad executable, so dependency violations are easy to introduce.

AzerothCore:

- `src/common` is a reusable platform library.
- `src/server/database` is a separate database library.
- `src/server/shared` contains auth/world shared protocol and realm code.
- `src/server/game` is a static game library.
- `src/server/scripts` builds static or dynamic script libraries.
- `src/server/apps/authserver` and `src/server/apps/worldserver` are thin executable entry points linked against those libraries.

Impact:

AzerothCore makes accidental coupling more visible. EQ2Emu has subsystem directories, but the build does not enforce them.

### 3. Database Lifecycle

EQ2Emu:

- README expects users to import external/latest SQL dumps for world and login databases.
- This repository contains generated database schema documentation under `docs/database`.
- No SQL migration/update files were found in the repo scan.
- World DB access is centralized in `WorldDatabase` plus subsystem DB files, but schema evolution is not first-class in this source tree.

AzerothCore:

- Carries base, create, custom, old, archive, updates, and pending update SQL directories.
- Separates auth, characters, and world databases.
- Has a `DBUpdater` component and update fetch/apply logic.
- Has SQL codestyle and update workflows.

Impact:

Database lifecycle is one of AzerothCore's biggest stability advantages. EQ2Emu would benefit more from an AC-style database migration/updater system than from almost any gameplay refactor.

### 4. Runtime Configuration

EQ2Emu:

- Uses `login_db.ini`, `world_db.ini`, `server_config.json`, and `log_config.xml`.
- Runtime files are expected under `server/`.
- Configuration is practical but relatively ad hoc.

AzerothCore:

- Uses distributed config templates, app configs, `conf/dist`, environment integration, CLI overrides, config merger tools, and documented severity policy.
- Can treat missing/unknown/invalid config as skip/warn/error/fatal depending on deployment mode.

Impact:

EQ2Emu should not necessarily copy the exact config format, but it would benefit from a typed config layer, documented config severity policy, config validation, and generated/default config installation.

### 5. Logging and Observability

EQ2Emu:

- Has `Log.*`, `LogTypes.h`, and XML logging configuration.
- Logging exists, but the architecture is smaller and less integrated with process policy.

AzerothCore:

- Has a log4j-like logger/appender model with hierarchical loggers, levels, console/file/database appenders, and documented configuration.
- Has metrics-related code under `src/common/Metric`.
- CI and tools reinforce log/config behavior.

Impact:

EQ2Emu already has enough logging structure to evolve incrementally. Borrowing the concept of hierarchical loggers, appenders, runtime validation, and test coverage is more useful than copying implementation.

### 6. Scripting and Content Extension

EQ2Emu:

- Uses Lua as a central content scripting mechanism.
- Runtime scripts are expected in folders such as `ItemScripts`, `Quests`, `RegionScripts`, `SpawnScripts`, `Spells`, `ZoneScripts`, and `PlayerScripts`.
- Lua API coverage is broad and heavily documented in `docs/lua_functions` and `docs/lua_api.md`.
- Packet layouts are partly data-driven through XML struct files.

AzerothCore:

- Uses C++ script classes and generated script loaders.
- Supports static or dynamic script modules.
- Has external modules under `modules/`, including a large `mod-playerbots` module.
- Has database-driven SmartAI and content SQL patterns in the broader architecture.

Impact:

EQ2Emu should preserve Lua as a core content feature. The transplantable idea is not "replace Lua with C++ scripts"; it is "make content and extensions load through stable, testable, isolated module boundaries." An EQ2 module system could package C++, Lua scripts, SQL updates, config defaults, and docs together.

### 7. Tests and CI

EQ2Emu:

- No active unit test tree or CI workflow was found in this repo scan.

AzerothCore:

- Has Google Test integrated by CMake.
- Test sources cover config, time, combat, spells, commands, entities, battleground logic, pools, and more.
- GitHub Actions build Linux/macOS/Windows variants, PCH/no-PCH, tools, modules, SQL style, and code style.

Impact:

This is a direct maturity gap. EQ2Emu does not need broad tests immediately, but it needs a test harness so risky systems can start accumulating regression coverage.

### 8. Tooling and Operations

EQ2Emu:

- Has `linux_compile.sh` that installs dependencies, builds third-party pieces, compiles servers, and copies runtime assets.
- Has runtime templates in `server/`.

AzerothCore:

- Has `apps/` for compiler helpers, config merger, database squash/export/update tools, Docker, installer, codestyle, startup scripts, test framework, valgrind, extractor tools, and CI support.
- Has `tools/`, `var/`, `.devcontainer`, Docker compose, and generated install paths.

Impact:

EQ2Emu can stay much smaller, but should add dedicated tool directories as soon as build, database, content, and release tasks become repeatable. One large installer script should gradually become smaller, testable tools.

## Can EQ2Emu Be Made More Like AzerothCore?

Yes, but it should be done in layers. Trying to reshape gameplay code first would be high-risk and low-return. The better path is to make the platform around EQ2Emu more mature, then use that foundation to gradually improve subsystem boundaries.

Recommended order:

1. Introduce a top-level CMake build while keeping existing makefiles temporarily.
2. Split targets into `common`, `loginserver`, `worldserver`, and eventually `database`, `lua`, `network`, `game`, and `tools`.
3. Add a minimal Google Test target linked against `common` first.
4. Add a database migration/update layout for login and world databases.
5. Add CI that only configures and compiles at first.
6. Add config validation and documented config policy.
7. Add a module packaging convention for optional EQ2 features.
8. Gradually reduce global ownership and giant facades as tests and libraries make boundaries enforceable.

This approach makes EQ2Emu more like AzerothCore where maturity matters: build reproducibility, update safety, testability, extension management, and operational clarity.

## What Can Be Extracted or Adapted From AzerothCore?

### Good Candidates for Conceptual Extraction

These are worth adapting, preferably by reimplementing for EQ2Emu:

- Top-level CMake layout and target graph.
- `common`/`database`/`game`/`scripts`/`apps` target separation.
- Build options for app selection, tests, tools, and optional modules.
- Generated revision/version header.
- Config installation and config merge tooling.
- Config severity policy: missing file, missing option, critical option, unknown option, invalid value.
- Hierarchical logger/appender model.
- Database updater workflow with base SQL, pending SQL, merged updates, archive, and table version tracking.
- SQL style checks and migration naming policy.
- Google Test harness and mocks for game entities/sessions.
- CI matrix for Linux, Windows, PCH/no-PCH, tools, and SQL checks.
- External module convention with module config, source, SQL, and scripts.
- Script loader generation pattern, adapted for Lua/C++ hybrid content.
- Tooling layout under `apps/` or `tools/` instead of monolithic install scripts.

### Possible Code Extraction, With Caution

The following AzerothCore components may be technically reusable only after license review and adaptation:

- CMake helper macros.
- DB updater concepts and file scanner logic.
- Config merger tools.
- Logger/appender implementation patterns.
- Test scaffolding ideas.
- Some generic utilities such as timers, string helpers, threading queues, or random utilities.

However, direct copying is not the default recommendation because:

- AzerothCore is repository-level GPL v2, while EQ2Emu is repository-level GPL v3.
- Some files may be GPL v2-or-later, but this must be verified per file.
- Even legally compatible utility code may be coupled to AzerothCore naming, build macros, config conventions, or global state.
- Reimplementing the pattern in EQ2Emu style will usually be cleaner than importing a dependency-heavy subsystem.

### Poor Candidates for Extraction

These should generally not be copied:

- WoW packet/session handlers.
- WoW opcode definitions.
- WoW spell/aura/combat implementation details.
- WoW entity hierarchy details where client behavior differs from EQ2.
- WoW SQL schemas and content tables.
- C++ content scripts for zones, bosses, spells, or quests.
- Map/grid assumptions tied to WoW client data formats.

These areas are too domain-specific. They can inspire organization, but not provide direct implementation.

## Specific Recommendations for EQ2Emu

### Phase 1: Build and Target Boundaries

Create a top-level `CMakeLists.txt` and target structure:

```text
source/common       -> eq2_common static library
source/LoginServer  -> eq2_login executable
source/WorldServer  -> eq2_world executable
tests               -> eq2_tests executable
tools               -> future tools
```

Keep the existing makefiles during transition. Once CMake builds both executables on Linux and Windows, deprecate the hand-built makefiles and Visual Studio artifacts.

### Phase 2: Database Lifecycle

Add a repository-owned database directory:

```text
database/
  login/
    base/
    updates/
    pending/
  world/
    base/
    updates/
    pending/
  custom/
```

Then add a simple updater that:

- Records applied migrations in each database.
- Applies pending or merged SQL in lexical order.
- Fails clearly on partial updates.
- Supports dry-run/list mode.
- Can be invoked by server startup later, but should start as a standalone tool.

This would close one of the largest maturity gaps with AzerothCore.

### Phase 3: Test Harness

Start with low-dependency tests:

- String utilities.
- Packet struct parsing from XML.
- Opcode mapping.
- Config parsing.
- Database migration ordering.
- Rule loading defaults.
- Lua registration table smoke tests.

Avoid trying to unit-test full zone simulation first. That will become easier after the build graph and mocks exist.

### Phase 4: Config and Logging Policy

Add a typed config facade over the existing JSON/INI/XML inputs:

- Required vs optional keys.
- Clear default values.
- Severity modes: relaxed, default, strict.
- Startup validation summary.
- Environment override support for deployment.

Then evolve logging toward named categories with appenders and runtime validation.

### Phase 5: Module/Content Packaging

Design an EQ2 module as a directory that can contain:

```text
module.json or CMakeLists.txt
src/
lua/
database/
config/
docs/
```

Initial modules can be static-only. Dynamic loading can wait. The important first step is a documented boundary where optional systems can live without modifying central world-server files.

### Phase 6: World Server Refactoring

After the platform work exists, begin reducing concentrated global state:

- Turn subsystem DB functions into service/repository classes where practical.
- Move packet handlers into per-system handler files consistently.
- Separate zone simulation, client session, persistence, and content callbacks through narrower interfaces.
- Add tests around each boundary before moving behavior.

Do not start here. It is the most invasive work and needs the previous phases to be safe.

## Risks and Constraints

- EQ2 packet/XML struct handling is a core differentiator. AzerothCore's packet model cannot simply replace it.
- EQ2's Lua API is a major asset. Replacing it would likely slow content development.
- The current world server uses many globals and long-lived loops; aggressive refactoring without tests could destabilize the server.
- Database schema may live outside this repo today. Bringing migrations into the repo requires coordination with whoever owns the SQL dumps.
- License compatibility must be checked before copying AzerothCore code.
- AzerothCore's maturity came from years of workflow discipline. EQ2Emu should adopt the minimum structure that improves stability without burying the project under process.

## Bottom Line

EQ2Emu can absolutely be made more like AzerothCore in the ways that matter most for stability: build reproducibility, dependency management, database migrations, tests, CI, configuration policy, logging, tools, and module boundaries.

The best extraction strategy is architectural, not literal. Use AzerothCore as a reference implementation for mature emulator project structure, but reimplement the pieces around EQ2's protocol, Lua content model, database reality, and current code ownership. The first high-value target should be a CMake build graph plus database migration/updater workflow; those two changes would unlock safer testing, CI, packaging, and future refactoring.
