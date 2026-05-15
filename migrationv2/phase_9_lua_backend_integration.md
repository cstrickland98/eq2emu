# Phase 9: Lua Backend Integration

Status: complete.

## Purpose

Attach a real Lua backend to the source2 scripting boundary while preserving owner-aware mutation rules and failure isolation.

## Where Work Begins

Start with:

- `source2/scripting/include/eq2/scripting/engine.h`
- `source2/scripting/tests/scripting_tests.cpp`
- Legacy references:
  - `source/WorldServer/LuaInterface.*`
  - `source/WorldServer/LuaFunctions.*`
  - Lua script directories under `server/` if present.
- Phase 8 zone owner and handoff behavior.

## Work Entailed

- Add Lua dependency discovery to CMake.
- Implement `ScriptBackend` using real Lua state.
- Define script loading paths and reload policy.
- Register a minimal source2-safe Lua API first.
- Route mutations through `OwnerCommandSink`.
- Convert Lua errors to logged `Result` failures.
- Ensure Lua calls do not require holding world or zone locks.
- Add smoke scripts for zone, spawn, item, quest, spell, player, and region event categories.
- Document unsupported legacy Lua APIs.
- Decide compatibility strategy for existing Lua scripts:
  - adapter layer,
  - staged API subset,
  - strict source2 API.

## Deliverables

- Lua-backed `ScriptBackend`.
- Lua CMake dependency wiring.
- Script loader/reloader.
- Minimal registered source2 Lua API.
- Tests for successful calls, errors, reloads, and owner-posted mutations.
- Compatibility matrix for legacy Lua APIs.

## Exit Criteria

- A real Lua script can be loaded, called, reloaded, and failed safely through source2 scripting.
- Script mutation requests are posted to owner sinks, not applied directly.
- Lua failures are isolated and logged.
- Legacy Lua API gaps are documented before gameplay features depend on them.

## Progress

- Added configure-time Lua runtime discovery in `source2/scripting/CMakeLists.txt`.
  - The build reports a discovered Lua 5.3 runtime DLL when present.
  - The backend can also use `EQ2_LUA_DLL` at runtime.
- Added `eq2/scripting/lua_backend.h`.
  - Runtime-loads the Lua C API on Windows without introducing a hard link dependency.
  - Creates an isolated Lua state per script call.
  - Loads and executes script source through the real Lua API.
  - Calls the requested Lua function by event name.
  - Converts Lua load/call failures into `Result` failures.
  - Reports a clean `unavailable` error when no Lua runtime can be loaded.
- Registered the first source2-safe Lua API subset:
  - `PostZoneMutation(command, id)` posts to `OwnerCommandSink`.
  - `ActorId()` exposes the current event actor id.
  - `TargetId()` exposes the current event target id.
  - `EventName()` exposes the current event function name.
- Added `eq2/scripting/script_loader.h` for script path policy:
  - category directory mapping,
  - `<root>/<category>/<name>.lua` path resolution,
  - source file loading with `Result` failures.
- Expanded `eq2_scripting_tests` with:
  - script path/source loading,
  - clean unavailable behavior,
  - real Lua load/call,
  - reload behavior through `ScriptEngine`,
  - Lua error isolation and logging,
  - missing function handling,
  - smoke calls for item, quest, spell, spawn, zone, player, and region event categories,
  - owner-posted mutation verification.

Compatibility notes:

- The source2 Lua API is intentionally strict and small. Existing legacy APIs listed under `docs/lua_functions/` are not implicitly available.
- Script mutations must post to owner sinks; Lua does not receive direct access to world or zone internals.
- The runtime loader is Windows-focused for this phase. Non-Windows builds receive an explicit unavailable result until a platform loader is added.
- Legacy Lua compatibility strategy is staged API subset first, then adapter functions as migrated gameplay features demand them.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_scripting_tests
ctest --test-dir build\source2 -C Debug -R scripting --output-on-failure
```

Verification run:

```powershell
cmake -S . -B build\source2
cmake --build build\source2 --config Debug --target eq2_scripting_tests
ctest --test-dir build\source2 -C Debug -R scripting --output-on-failure
```
