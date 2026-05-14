# Phase 1 Lua Inventory

Status: complete.

## Known Areas To Inventory

| Area | Current files to inspect |
| --- | --- |
| Lua engine wrapper | `source/WorldServer/LuaInterface.*` |
| Lua API bindings | `source/WorldServer/LuaFunctions.*` |
| Client-triggered script calls | `source/WorldServer/client.cpp` |
| Zone script calls | `source/WorldServer/zoneserver.cpp` |
| Combat/script interactions | `source/WorldServer/Combat.cpp` |
| Item, quest, spell scripts | `server/ItemScripts`, `server/Quests`, `server/Spells` if present |
| Spawn, region, zone scripts | `server/SpawnScripts`, `server/RegionScripts`, `server/ZoneScripts` if present |

## Known Script Categories

- Item scripts.
- Quest scripts.
- Spawn scripts.
- Zone scripts.
- Player scripts.
- Region scripts.
- Spell scripts.

## Current Script State Model

`LuaInterface` owns multiple script maps:

- Quest ID to `lua_State*`.
- Item script name to one or more `lua_State*`.
- Spawn script name to one or more `lua_State*`.
- Zone script name to one or more `lua_State*`.
- Player script name to one or more `lua_State*`.
- Region script name to one or more `lua_State*`.
- Spell script name to one or more `LuaSpell*`.

Each script category also has a mutex map or category-level mutex. Stale userdata is tracked separately.

## Current Call Sites And Events

| Category | Example current calls |
| --- | --- |
| Zone | `player_entry`, `player_loadcomplete`, `enter_location`, `signal_changed`, `new_client`, `preinit_zone_script`, `init_zone_script` |
| Item | `equipped`, `obtained`, `removed`, `buyback_display_flags`, `buy_display_flags`, `item_difficulty`, `item_description`, `placed`, `cast`, `used`, `examined`, `destroyed` |
| Quest | `Accepted`, `Declined`, `Deleted`, `Reload`, `CurrentStep`, step complete action, quest complete action |
| Player | `on_level_up`, `on_level_up_complete`, `on_tradeskill_level_up`, `on_tradeskill_level_up_complete`, `on_mail_first_read` |
| Spawn | Direct spawn script function calls such as `can_use_command` |
| Combat/spells | Proc and spell script calls from combat and entity paths |

## Exposed API Categories

The registered Lua API is broad and includes:

- Spawn stat mutation: HP, power, position, heading, model, class, mount, speed, faction, attackable state.
- Spawn lookup and movement: spawn lists, group IDs, rail IDs, movement loops, move to location.
- Combat: damage, healing, cast spell, hate, attack, interrupts, stealth/invisibility.
- Inventory and item mutation: add, summon, remove, equip, unequip, item lookup.
- Zone and travel: get zone, zone by script, bind/gate checks, safe location.
- Conversation and messaging: say, shout, emote, conversations, popups.
- Quest mutation: register, offer, delete, steps, progress, rewards, prerequisites.
- Spell book and skill effects.
- Loot and coin.
- Timers and proximity functions.

This API shape means Lua is currently a gameplay mutation surface, not just a scripting callback layer.

## Initial Risks

- Script calls appear in client, combat, and zone paths.
- Script-specific mutexes are managed in `LuaInterface`.
- Some script calls may happen while other gameplay locks are held.
- Lua can likely mutate world/zone/player state directly through exposed APIs.
- Combat currently obtains script mutexes and calls Lua from combat processing.
- Zone startup calls Lua during `ZoneServer::Process` while zone loading state is active.
- Client login invokes zone Lua after adding the player to the zone.
- Lua reload commands destroy and recreate script states while clients/zones may be active.

## Source2 Boundary Goal

Lua should be called through a controlled scripting service:

```text
owner executor -> scripting service -> script context -> approved operations
```

For mutations:

```text
script operation -> command posted to owning zone/world executor
```

## Required Source2 Script Contexts

| Context | Owner |
| --- | --- |
| `ZoneScriptContext` | Zone executor |
| `SpawnScriptContext` | Zone executor |
| `ItemScriptContext` | Player/session owner or zone executor depending on action |
| `QuestScriptContext` | Player/session owner with zone command handoff when needed |
| `SpellScriptContext` | Zone combat executor |
| `PlayerScriptContext` | Player/session owner |
| `RegionScriptContext` | Zone executor |

## Remaining Review Questions

- Inventory every `Run*Script` and `CallQuestFunction` call site.
- Identify script calls made under locks.
- Identify all APIs exposed by `LuaFunctions`.
- Group exposed APIs by owner: player, zone, spawn, item, quest, spell, world.
- Decide which APIs should return immediate values and which should enqueue commands.
- Identify script reload and stale userdata behavior.

Phase 1 identified the categories and architectural problem. Full API-by-API migration should happen during phase 10 and gameplay-system migrations.

## Phase 1 Lua Exit Result

Lua inventory is sufficient for phase 2 planning. Source2 must treat Lua as an owner-aware scripting boundary and must not allow script calls to freely mutate shared world/zone/player state from arbitrary threads.
