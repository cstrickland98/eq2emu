# Phase 8: Zone Bootstrap Live Slice

Status: complete.

## Purpose

Move from an abstract zone command queue to a minimally live source2 zone runtime that can bootstrap zone metadata from DB, admit a selected character, and produce stable update snapshots.

## Where Work Begins

Start after Phase 7 proves world handoff. Use:

- `source2/zone/include/eq2/zone/runtime.h`
- `source2/world/include/eq2/world/session.h`
- Zone bootstrap repository from Phase 4.
- Legacy references:
  - `source/WorldServer/zoneserver.*`
  - zone bootstrap SQL identified in Phase 1 database inventory.

## Work Entailed

- Define zone runtime configuration and lifecycle.
- Load zone metadata through source2 DB repository.
- Create or adapt zone admission request handling from world.
- Create player/spawn bootstrap placeholders tied to real IDs.
- Add a minimal zone tick loop owner.
- Produce immutable snapshots and client fanout updates.
- Add movement/combat placeholder command handling only where already characterized.
- Keep Lua calls behind the scripting boundary.
- Add smoke test that selects a character and admits it into a zone runtime.

## Deliverables

- Zone bootstrap service.
- World-to-zone admission implementation.
- Zone tick owner lifecycle.
- Minimal player/spawn state creation.
- Snapshot/update tests.
- Live-ish smoke path from character select to zone admission.

## Exit Criteria

- A known character can be admitted into a source2 zone runtime through `ZoneHandoff`.
- Zone metadata is loaded through a DB repository, not hard-coded fixtures.
- Zone state is still mutated only by the zone owner.
- External systems post commands or consume snapshots.
- Missing spawn serialization/client update details are documented before gameplay migration.

## Progress

- Added `eq2/zone/bootstrap.h` with:
  - zone runtime config,
  - DB-backed zone metadata loading,
  - zone admission request/result types,
  - zone runtime creation on demand,
  - selected-character admission,
  - player spawn placeholder creation tied to the real character id,
  - client subscription on admission,
  - immutable snapshot access.
- Added `eq2/world/zone_handoff_adapter.h`.
  - Implements `ZoneHandoff` by adapting world handoff requests into `ZoneBootstrapService` admissions.
  - Keeps world code from owning or mutating zone internals.
- Expanded `eq2_zone_runtime_tests` with DB-backed bootstrap coverage:
  - `SqlZoneBootstrapRepository` boundary usage,
  - safe-location mapping,
  - missing-zone rejection,
  - admitted player spawn snapshot,
  - client subscription snapshot.
- Expanded `eq2_world_session_tests` with a live-ish select-to-zone path:
  - SQL-backed character list,
  - character select,
  - world-to-zone handoff adapter,
  - SQL-backed zone metadata load,
  - zone runtime snapshot containing the admitted player spawn.

Compatibility notes:

- Spawn serialization and full client fanout packet generation are not implemented yet. The runtime produces stable snapshots and update records for later packet work.
- The admitted player spawn is a placeholder with character id, name, safe location, and baseline hit points. Full player stat/equipment/spawn hydration remains future feature migration work.
- Lua hooks remain behind the scripting boundary and are not called from the zone bootstrap path until Phase 9 scripting integration is in place.

## Verification Commands

```powershell
cmake --build build\source2 --config Debug --target eq2_zone_runtime_tests eq2_world_session_tests
ctest --test-dir build\source2 -C Debug -R "zone|world" --output-on-failure
```

Verification run:

```powershell
cmake --build build\source2 --config Debug --target eq2_zone_runtime_tests eq2_world_session_tests
ctest --test-dir build\source2 -C Debug -R "zone|world" --output-on-failure
```
