# Phase 12: First Feature Migration Pilot

Status: complete.

## Purpose

Migrate one narrow gameplay feature only after Phase 11 confirms source2 is ready. The pilot should validate the feature migration workflow before broad systems are moved.

## Where Work Begins

Start only after Phase 11 has a go decision. The default pilot candidates are:

- Character list/select parity hardening.
- Zone admission and initial spawn snapshot.
- Spawn lifecycle add/move/remove.
- A small item inspection or item script event.
- A small quest accept/update path.

Choose one pilot based on risk, available characterization coverage, and live smoke value.

## Work Entailed

For the selected feature:

- Write or update current behavior notes.
- Identify legacy files/functions involved.
- Add or extend characterization tests.
- Define source2 owner:
  - player session,
  - world service,
  - zone executor,
  - scripting service,
  - DB repository.
- Implement source2 feature behavior behind the established boundaries.
- Add DB migrations if schema support is needed.
- Add Lua API support if script behavior is involved.
- Add smoke coverage for the user-visible path.
- Document compatibility gaps and rollback strategy.

## Deliverables

- Feature-specific migration notes in this file or a linked feature file.
- Source2 implementation for the selected narrow feature.
- Tests covering normal, failure, and legacy edge behavior.
- Smoke check for the live path where practical.
- Documentation of remaining legacy behavior.

## Exit Criteria

- The selected feature works through source2 runtime boundaries.
- Tests cover the feature's characterized legacy behavior.
- Any DB changes are represented in migration tooling.
- Any scripting behavior uses the source2 Lua boundary.
- Live smoke coverage proves the feature through the relevant app path or an accepted test harness.
- The feature can be enabled/disabled or rolled back without deleting legacy production behavior.

## Selected Pilot

Zone safe-location/player-admission feature slice.

Legacy references:

- `source/WorldServer/zoneserver.*`
- `source/WorldServer/WorldDatabase.*`
- `docs/code/zoneserver.md`
- `docs/database/world/zones.md`
- `docs/lua_functions/GetCurrentZoneSafeLocation.md`
- `docs/lua_functions/Zone.md`

## Source2 Ownership

- DB repository: `SqlZoneBootstrapRepository`
- World service: creates handoff request after character select
- Zone executor: owns admission mutation and player spawn creation
- Scripting service: optional Lua hook through `ScriptEngine`
- Client packet path: not part of this pilot

## Progress

- Added `eq2/zone/admission_feature.h`.
  - Validates account id, character id, zone id, and access key before admission.
  - Admits through `ZoneBootstrapService`.
  - Preserves zone-owner mutation rules by using zone commands and snapshots.
  - Optionally calls a Lua zone hook through `ScriptEngine`.
  - Supplies the admitted spawn safe location to Lua through `GetCurrentZoneSafeLocation`.
  - Records Lua hook errors without rolling back accepted zone admission.
- Extended `eq2_zone_runtime_tests` with pilot coverage:
  - normal DB-backed admission,
  - missing zone metadata rejection,
  - invalid account id,
  - invalid character id,
  - invalid access key,
  - Lua hook success through `OwnerCommandSink` and `GetCurrentZoneSafeLocation`,
  - Lua hook failure isolation.
- Updated `eq2_world_server --smoke-world-live`.
  - The smoke now registers world with source2 login,
  - validates login handoff,
  - loads character list,
  - selects the character,
  - admits the character into a DB-backed zone runtime,
  - verifies the resulting spawn snapshot.

DB migration status:

- No schema changes were required. The pilot uses the existing `zones` metadata boundary.

Compatibility notes:

- The pilot does not serialize spawn packets to a real client.
- The Lua hook uses the strict source2 API subset, not the full legacy Lua API.
- The player spawn placeholder does not hydrate equipment, stats, buffs, quests, or visual state.

Rollback strategy:

- Disable the Phase 12 feature path by using `ZoneBootstrapService` directly without `ZoneAdmissionFeature`.
- Keep legacy zone admission untouched.
- No SQL rollback is required.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_zone_runtime_tests eq2_world_server eq2_world_session_tests
ctest --test-dir build\source2 -C Debug -R "zone|world" --output-on-failure
build\source2\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```

Verification run:

```powershell
cmake --build build\source2 --config Debug --target eq2_zone_runtime_tests eq2_world_server eq2_world_session_tests
ctest --test-dir build\source2 -C Debug -R "zone|world" --output-on-failure
build\source2\source2\apps\Debug\eq2_world_server.exe --smoke-world-live
```
