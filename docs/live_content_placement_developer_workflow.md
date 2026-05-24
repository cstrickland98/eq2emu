# Live Placement Developer Workflow

## Implementation Checklist

- Add only GM/dev command handlers and editor session state.
- Keep preview state separate from house-item placement state.
- Persist through `SaveSpawnInfo` and `SaveSpawnEntry`.
- Preserve groundspawn metadata when saving harvestable or collection previews.
- Move existing persisted spawns through `UpdateSpawnLocationSpawns`.
- Delete saved placements by `spawn_location_placement.id`.
- Use `/reload spawns` for live refresh.
- Use existing `/spawn group` and `/location` commands for camps, spawn groups, and patrol markers.
- Do not change packet serialization, combat, quest, loot, AI, or client assets.

## Dependency Graph

```text
Commands
  -> Client editor state
  -> ZoneServer::AddSpawn / RemoveSpawn
  -> Client::SendMoveObjectMode
  -> WorldDatabase placement writes
  -> existing /spawn group and /location workflows for grouped content

OP_PositionMoveableObject
  -> Client dev preview branch
  -> existing house placement branch
  -> existing persisted spawn update branch

WorldDatabase
  -> spawn template tables
  -> spawn_location_name
  -> spawn_location_entry
  -> spawn_location_placement
```

## Rollback Plan

- Unsaved preview rollback: `/spawndelete`, `/spawnpreview cancel`, `/devmode off`, or canceling move-object mode removes the preview from the zone.
- Saved placement rollback: target the saved spawn and run `/spawndelete`.
- Zone-state rollback: run `/reload spawns` to rebuild the current zone from DB state.
- Full DB rollback: restore the operator's normal world DB backup if broader content edits need to be reverted.

## Testing Plan

Static validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\validate-live-placement.ps1
```

Build validation:

```powershell
cmake -S . -B build\ralph -DEQ2EMU_BUILD_LOGIN=OFF -DEQ2EMU_BUILD_WORLD=ON
cmake --build build\ralph --config Debug --target eq2world
```

Runtime validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\live-placement-db-smoke.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\live-placement-launch-check.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\live-placement-full-stack-check.ps1
```

The validation scripts read `database-connection.txt` by default. They connect to the populated MariaDB instance, temporarily point server INI files at that connection during launch checks, restore the original INI files in `finally`, and avoid manual SQL.

The database smoke script inserts marked object/NPC/harvestable/collection placements, updates position fields, verifies duplicate and invalid-zone rejection, creates a spawn group, creates patrol markers, verifies the rows on fresh DB connections, and cleans up all marked rows.

The Windows MariaDB connector is initialized with peer verification disabled by default because the provided MariaDB instance has SSL disabled. This only affects database connection setup; placement tooling still uses normal DB permissions and does not alter gameplay systems.

Current validation coverage and the remaining in-client completion gate are tracked in `docs/live_content_placement_validation_matrix.md`.

In-client workflow validation:

1. Start worldserver against a disposable world DB.
2. Log in with a GM account.
3. Run `/devmode on`.
4. Run `/spawnpreview object 1472 0 1 placement_static_test`.
5. Move the preview and confirm it moves without a DB row before `/spawnsave`.
6. Run `/spawnsave placement_static_test`.
7. Confirm rows exist in `spawn`, the matching type table, `spawn_location_name`, `spawn_location_entry`, and `spawn_location_placement`.
8. Run `/reload spawns` and confirm the placement remains.
9. Restart worldserver and confirm the placement remains.
10. Run `/spawndelete`, `/reload spawns`, and confirm the placement is gone.
11. Clone or create a `harvestable`/`collection` preview, save it, and confirm the `spawn_ground` row preserves `groundspawn_id` and `collection_skill`.
12. Save two NPC previews, group them with `/spawn group create` and `/spawn group add`, run `/reload spawns`, and confirm only the grouped camp behavior changed.
13. Create patrol markers with `/location create` and `/location add`, run `/reload locations`, and confirm the points reload without changing NPC AI scripts.

Safety validation:

- Run each command with a non-GM account and confirm it is blocked.
- Try `/spawnsave` with no preview and no persisted target and confirm no DB write occurs.
- Repeatedly create, cancel, clone, save, reload, and delete previews and watch logs for crashes or leaked duplicate placements.
- Keep repeated move/save cycles under the expected interactive threshold of 250 ms on local DB hardware.

Regression validation:

- Load an existing zone before and after placement edits.
- Confirm existing NPCs spawn from their previous rows.
- Confirm quests, combat, loot, and AI behavior are unchanged by the placement workflow.
