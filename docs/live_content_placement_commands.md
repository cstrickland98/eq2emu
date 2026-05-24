# Live Placement Commands

All placement commands require GM status 100 or higher. The worldserver also registers fallback command entries at startup if the world database does not contain these rows.

## Commands

`/devmode on`

Enables live placement mode for the current GM session.

`/devmode off`

Disables placement mode and discards any active unsaved preview.

`/spawnpreview [object|npc|sign|groundspawn|harvestable|collection] [model] [type args...] [name]`

Creates a non-persistent preview at the GM position and opens client move-object mode. With no arguments, it creates a generic object preview.

For NPCs, type args are `[class] [level] [name]`.

For harvestables and collections, type args are `[groundspawn_id] [skill] [name]`. `skill` may be `Mining`, `Gathering`, `Fishing`, `Trapping`, `Foresting`, `Collecting`, or `Unused`. The `collection` type defaults skill to `Collecting`.

`/spawnpreview cancel`

Discards the active preview without touching the database.

`/spawnpreview clone [name]`

Clones the targeted non-player spawn into a non-persistent preview at the GM position.

`/spawnclone [name]`

Alias workflow for cloning the targeted non-player spawn into a preview.

`/spawnsave [location_name] [spawn_percent]`

Persists the active preview to `spawn`, its type table, `spawn_location_name`, `spawn_location_entry`, and `spawn_location_placement`. If no preview exists and the GM targets a persisted spawn, it updates that spawn placement row with the target's current coordinates.

`/spawndelete`

Discards the active preview. If no preview exists, deletes the targeted persisted placement row and removes the live spawn from the zone. If that placement was the final DB reference for a tool-created spawn definition, the orphaned `spawn` and type-table rows are removed too.

## Sample Workflows

Create a ground object:

```text
/devmode on
/spawnpreview object 1472 0 1 crate_preview
move the preview in the client
/spawnsave qeynos_crate_cluster
/reload spawns
```

Create a harvestable node from an existing groundspawn table:

```text
/devmode on
/spawnpreview harvestable 1472 42 Mining ore_node_preview
move the preview in the client
/spawnsave antonica_ore_node
/reload spawns
```

Create a collection node:

```text
/devmode on
/spawnpreview collection 1472 84 shiny_leaf
move the preview in the client
/spawnsave antonica_shiny_leaf
/reload spawns
```

Clone an existing NPC placement:

```text
/devmode on
target the existing NPC
/spawnclone cloned_guard
move the clone in the client
/spawnsave guard_post_clone
```

Build an enemy camp:

```text
/devmode on
/spawnpreview npc 203 1 10 camp_guard_a
move the preview in the client
/spawnsave camp_guard_a
/spawnpreview npc 203 1 10 camp_guard_b
move the preview in the client
/spawnsave camp_guard_b
target the first saved camp spawn
/spawn group create camp_guard_group
target the second saved camp spawn
/spawn group add <group_id>
/reload spawns
```

Discard a preview:

```text
/devmode on
/spawnpreview npc 203 1 10 test_guard
/spawndelete
```

Delete a saved placement:

```text
/devmode on
target the persisted spawn
/spawndelete
/reload spawns
```

Edit an existing placement:

```text
/devmode on
target the persisted spawn
/spawn move
move the spawn in the client
```

The existing move-object path saves the targeted spawn coordinates through `WorldDatabase::UpdateSpawnLocationSpawns`.

Create patrol/location markers for script-driven movement:

```text
/location create camp_patrol 1
/location add <location_id>
move the GM to each patrol point
/location add <location_id>
/reload locations
```

NPC movement behavior remains script-driven through existing Lua movement APIs. The placement tooling records the in-world points without changing AI behavior.
