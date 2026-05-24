# Live Content Placement Architecture

This document records the Ralph read/analyze/layout deliverables for live in-game content placement.

## Existing Spawn Architecture

World spawns are split between template tables and placement tables:

- `spawn` stores shared spawn identity and appearance fields.
- Type tables store type-specific data: `spawn_npcs`, `spawn_objects`, `spawn_widgets`, `spawn_signs`, and `spawn_ground`.
- `spawn_location_name` names a location group.
- `spawn_location_entry` links one or more spawn templates to a location group with spawn percentages.
- `spawn_location_placement` stores concrete world placement: zone, instance, x/y/z, heading, pitch, roll, respawn, grid, expire, and stat overrides.
- `spawn_location_group`, `spawn_location_group_chances`, and `spawn_location_group_associations` layer group behavior on placement ids.
- `spawn_ground`, `groundspawns`, and `groundspawn_items` define harvestable and collection-node behavior separately from the physical placement.
- `locations` and `location_details` store reusable in-world points; NPC movement still comes from existing scripts and Lua movement APIs.

## Placement Load Flow

```text
ZoneServer starts or reloads
  -> WorldDatabase::LoadSpawns(zone)
  -> WorldDatabase::ProcessSpawnLocations(...)
  -> SpawnLocation structs are populated from spawn_location_placement + spawn_location_entry + type table
  -> ZoneServer::AddSpawnLocation(...)
  -> ZoneServer processes location/group data into live Spawn instances
```

The placement id from `spawn_location_placement.id` is copied onto each live `Spawn` as `Spawn::spawn_location_spawns_id`.

## Persistence Flow

```text
Existing persisted spawn move
  -> OP_PositionMoveableObject or /spawn move myloc
  -> Spawn x/y/z/heading updated
  -> WorldDatabase::UpdateSpawnLocationSpawns(spawn)
  -> spawn_location_placement row updated by placement id

New placement preview save
  -> /spawnpreview creates unsaved live Spawn
  -> OP_PositionMoveableObject updates preview only in memory
  -> /spawnsave
  -> WorldDatabase::SaveSpawnInfo(preview)
  -> WorldDatabase::SaveSpawnEntry(preview, location name, percent, offsets)
  -> spawn, type table, spawn_location_name, spawn_location_entry, and spawn_location_placement rows created
```

## Reload Flow

```text
/reload spawns
  -> Commands::Process(COMMAND_RELOAD_SPAWNS)
  -> ZoneServer::ReloadSpawns()
  -> existing live spawn data is cleared/rebuilt
  -> WorldDatabase::LoadSpawns(zone)
  -> saved placements reappear from database state
```

The placement tool deliberately uses the existing reload path. No packet format, combat, quest, loot, or AI reload behavior is changed.

## Safe Extension Points

- `Commands` is the command boundary for GM-only placement actions.
- `Client` owns per-GM editor session state: dev placement mode and a single preview spawn pointer.
- `OP_PositionMoveableObject` already carries client-side placement coordinates; the tool branches preview handling before house-item persistence.
- `WorldDatabase::SaveSpawnInfo`, `SaveSpawnEntry`, `UpdateSpawnLocationSpawns`, and `RemoveSpawnPlacement` are the only DB write paths used by placement tooling.
- `/reload spawns` remains the live refresh mechanism.
- `/location` and `/reload locations` remain the existing patrol/location-marker refresh mechanism.

## Existing GM Capabilities

- `/spawn create` creates live, unsaved NPC/object/sign/groundspawn instances at the GM position.
- `/spawn add` persists a targeted unsaved spawn into spawn template/location/placement tables.
- `/spawn move` starts client move-object placement for a targeted spawn.
- `/spawn move myloc` moves a persisted targeted spawn to the GM position and updates DB placement.
- `/spawn set` edits spawn fields and persists either template or placement fields.
- `/spawn remove` removes a spawn through the legacy location-level delete path.
- `/spawn group` manages spawn groups and associations.
- `/spawn combine` combines multiple spawns into one spawn location.
- `/location create/add/remove/delete/list` manages persisted in-world location markers.
- `/reload spawns` reloads the current zone spawn set without a worldserver restart.
- `/reload groundspawns` and `/reload locations` refresh supporting harvest/location data.
- `/sle` edits whitelisted spawn placement or entry columns on the targeted spawn.

## Placement Tool Design

The placement tool adds a narrow editor layer:

- `/devmode on|off` gates all placement workflow commands behind GM status and explicit editor intent.
- `/spawnpreview` creates a non-persistent preview spawn in the current zone.
- `/spawnpreview harvestable` and `/spawnpreview collection` create groundspawn previews with persisted `spawn_ground` metadata.
- `/spawnclone` clones the targeted non-player spawn into a non-persistent preview at the GM position.
- Client move-object packets update preview coordinates in memory only.
- `/spawnsave` persists the active preview through the existing spawn save APIs.
- `/spawndelete` discards an active preview or deletes the targeted persisted placement row.
- Enemy camps and spawn groups are built by saving multiple previews, then using the existing `/spawn group` commands on those saved placements.
- Patrol points are built with existing `/location` commands; this tooling records points but does not alter NPC AI.

## Command Flow

```text
GM: /devmode on
  -> Client::SetDevPlacementMode(true)

GM: /spawnpreview ...
  -> Commands::Command_SpawnPreview
  -> create Spawn subtype
  -> ZoneServer::AddSpawn(preview)
  -> Client::SetDevPlacementPreviewSpawn(preview)
  -> Client::SendMoveObjectMode(preview)

Client submits placement
  -> OP_PositionMoveableObject
  -> preview coordinates updated in memory
  -> no DB write

GM: /spawnsave
  -> Client::SaveDevPlacementPreview
  -> SaveSpawnInfo + SaveSpawnEntry
  -> preview becomes normal persisted spawn
```

## Entity Lifecycle

```text
No editor state
  -> dev mode enabled
  -> preview created
  -> preview moved any number of times
  -> saved: DB ids assigned and preview pointer cleared
  -> or discarded: preview removed from zone and deleted
```

Preview entities are isolated from house-item placement state and do not use `Client::tempPlacementSpawn`.

## DB Persistence Flow

```text
SaveSpawnInfo
  -> insert spawn row when database id is 0
  -> insert matching type table row

SaveSpawnEntry
  -> create spawn_location_name if needed
  -> insert spawn_location_entry
  -> insert spawn_location_placement
  -> copy inserted placement id back onto Spawn
```

## Rollback Strategy

- Before saving, `/spawndelete` or client cancel removes the preview with no DB writes.
- After saving, target the spawn and use `/spawndelete` to remove only the current `spawn_location_placement.id`.
- If that placement was the last placement for its location, the tool removes its location entries and name to avoid orphaned editor-created rows.
- Existing database backups remain the recovery path for broader operator mistakes.

## Non-Goals Preserved

The implementation does not modify packet serialization, combat, quests, NPC AI, loot, rendering, client assets, DLL behavior, or the core spawn loader.
