# Live Placement Validation Matrix

This matrix tracks the Ralph exit strategy against current automated evidence.

## Proven By Automation

- Build stability: `cmake --build build\windows-msvc --target eq2world login` succeeds.
- Static implementation coverage: `scripts\validate-live-placement.ps1` checks command registration, GM guards, preview state, move-object handling, DB delete cleanup, reload timer safety, groundspawn metadata persistence, docs, and validation scripts.
- Database persistence paths: `scripts\live-placement-db-smoke.ps1` connects through `database-connection.txt`, inserts marked object/NPC/harvestable/collection placements, updates position and heading, verifies duplicate and invalid-zone rejection, creates a spawn group, creates patrol markers, verifies rows on fresh DB connections, deletes a placement, verifies cascade behavior, and removes all marked rows.
- Worldserver launch: `scripts\live-placement-launch-check.ps1` temporarily points `world_db.ini` at the provided MariaDB instance, verifies startup, observes the process, stops it, and restores the original INI.
- Login plus world launch: `scripts\live-placement-full-stack-check.ps1` temporarily points `login_db.ini` and `world_db.ini` at the provided MariaDB instance, verifies both processes stay running, stops them, and restores the original INI files.
- Remote MariaDB compatibility: `DBcore` initializes the MariaDB client so the provided SSL-disabled server can be used without external environment setup.

## Covered By Code Review And Static Checks

- Non-GM safety: placement commands require `GetAdminStatus() >= 100` and an active zone before acting.
- Preview isolation: previews are stored in `Client::devPlacementPreviewSpawn`, separate from house `tempPlacementSpawn`.
- Non-persistent preview movement: `OP_PositionMoveableObject` updates dev preview coordinates in memory and does not call DB persistence until `/spawnsave`.
- Preview despawn: `/spawnpreview cancel`, `/spawndelete`, `/devmode off`, move-object cancel, client zone removal, and zone spawn removal clear preview state.
- Save path: `/spawnsave` uses `SaveSpawnInfo` and `SaveSpawnEntry` so saved previews follow existing spawn tables and reload paths.
- Existing placement edit path: persisted targeted spawns continue through `UpdateSpawnLocationSpawns`.
- Clone/delete: `/spawnclone` creates unsaved previews from target spawn copies; `/spawndelete` removes an active preview or deletes the targeted `spawn_location_placement.id`, associated group/persisted-respawn rows, and orphaned tool-created spawn definitions after the final placement reference is gone.
- Reload safety: `/reload spawns` now defers deletion of in-memory spawn prototypes until the zone is in the reload phase and suppresses respawn/expire timers while reload data is being rebuilt.
- Enemy camps and patrol points: saved placements use existing `/spawn group` commands; patrol points use existing `/location` commands.

## Not Proven Autonomously

- A real GM client session entering `/devmode on`, creating a preview, visually seeing it appear, moving it with the black-box client, saving it, reloading the zone, restarting the worldserver, and visually confirming persistence.
- Client-side move-object latency under 250 ms from a rendered EQ2 client.
- Existing quests, combat, and NPC AI functioning from inside the client after placement edits.

The missing items require interactive EQ2 client control and observation. Server-side build, DB, command-path, and process-launch evidence is in place, but these client-visible workflows remain the completion gate for the full Ralph exit strategy.
