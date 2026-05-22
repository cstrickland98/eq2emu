### Command: /modelviewer

**Handler:** `COMMAND_MODEL_VIEWER`

**Required status:** 200

GM model preview helper that searches model metadata and spawns temporary client-rendered NPC previews.
The world server registers this command as a built-in fallback, so a `commands` table row is not required.

**Usage:**

- `/modelviewer list <name|model_id>` - Searches the `eq2models` table.
- `/modelviewer npc <name|spawn_id|model_id>` - Searches NPC spawn rows and shows their model ids.
- `/modelviewer preview <model_id> [size] [level] [name]` - Spawns a temporary preview NPC at your current location.
- `/modelviewer clear` - Removes the targeted preview spawn.

Preview spawns are not saved to the database and expire automatically after 120 seconds.
