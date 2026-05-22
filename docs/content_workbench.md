# EQ2Emu Content Workbench

The World Server now serves a no-auth browser dashboard at:

```text
/ or /ui
```

The content workbench is linked from that dashboard and is also available directly at:

```text
/content
```

These pages are intended for local GM/content development workflows. They provide route navigation, server status views, zone inspection, validation, map visualization, and simple builders for common content rows.

## Routes

- `/` and `/ui` - Accessible World Server dashboard.
- `/routes` - Route metadata used by the dashboard route explorer.
- `/content` and `/content/` - Accessible content workbench.
- `/content/api/bootstrap` - Zone list and online editor positions.
- `/content/api/zone` - Manifest, map points, audit issues, and quest validation for one zone.
- `/content/api/search` - Model metadata and existing NPC model search.
- `/content/api/apply` - Dry-run and apply endpoint for builders.

The dashboard, content workbench, and World Server web routes are registered with `auth_required = false`; no `webhardcodeuser`, `webhardcodepassword`, `web_users`, or `web_routes` setup is required for these pages.

## Access

Set `WorldServer.webaddress` and `WorldServer.webport` in `server_config.json`, rebuild, and restart the World Server. Example:

```json
"WorldServer": {
  "webaddress": "127.0.0.1",
  "webport": "8080",
  "webcertfile": "",
  "webkeyfile": "",
  "webhardcodeuser": "",
  "webhardcodepassword": ""
}
```

Then open:

```text
http://127.0.0.1:8080/
```

## Included Tools

- Zone content manifest: NPCs, enemies, harvestables, quests, POIs, merchants, transporters, and placement counts.
- Zone map overlay: database placement points projected by X/Z extents with filters for NPCs, harvestables, widgets, signs, and objects.
- Content QA: missing spawn entries, hostile NPCs without loot, harvest nodes without items, broken quest item references, duplicate spawn names, and missing quest script paths.
- Quest graph validation: starter presence, item references, prerequisite quest references, and script path checks.
- Online editor position picker: uses connected player positions to fill placement forms.
- Model search and preview-to-real flow: searches `eq2models` and existing NPC models, then copies the model into the NPC builder.
- NPC/encounter builder: creates `spawn`, `spawn_npcs`, `spawn_location_name`, `spawn_location_entry`, and `spawn_location_placement` rows.
- Harvestable node builder: creates `groundspawns`, `spawn`, `spawn_ground`, `groundspawn_items`, and placement rows.
- POI builder: creates `locations` and `location_details`.
- Item and loot builder: creates an item, loot table, loot drop, and optional spawn loot attachment.
- Dialogue/hail editor: generates a Lua hail script, can write it under `SpawnScripts/ContentWorkbench`, and can attach it through `spawn_scripts`.

## Apply Flow

Each builder has a preview button and an apply button. Preview returns the plan without writing. Apply writes rows or files immediately and then refreshes the loaded zone.

The UI intentionally does not create users, sessions, permissions, or any authentication configuration.
