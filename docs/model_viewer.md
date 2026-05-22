# EQ2 Model Viewer

The server does not carry EQ2 mesh assets, so the model viewer uses the EQ2 client renderer. GMs can search known model ids, spawn a temporary preview NPC, inspect it in-game, and then place or discard the real spawn.

## Slash Command

Use `/modelviewer` in-game with GM status 200 or higher.
The world server registers this command as a built-in fallback, so it does not require a new row in the `commands` table.

```text
/modelviewer list gnoll
/modelviewer npc orc
/modelviewer preview 1234 32 10 "Gnoll preview"
/modelviewer clear
```

Preview spawns:

- are not written to `spawn`, `spawn_npcs`, or placement tables;
- are named with a `[Preview]` prefix;
- expire after 120 seconds;
- can be removed early by targeting them and running `/modelviewer clear`.

## Data Sources

- `/modelviewer list` searches `eq2models`.
- `/modelviewer npc` searches `spawn` joined to `spawn_npcs`.
- `/modelviewer preview` accepts any model id supported by the current `Spawn::SetModelType` appearance field.
