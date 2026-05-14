# Phase 1 Database Inventory

Status: complete.

## Known Database Configs

| Database | Config file | Current references |
| --- | --- | --- |
| Login DB | `login_db.ini` | `source/common/Common_Defines.h`, `source/common/DatabaseNew.cpp`, `source/common/dbcore.h` |
| World DB | `world_db.ini` | `source/common/Common_Defines.h`, `source/common/DatabaseNew.cpp`, `source/common/dbcore.h` |

## Known Access Areas

| Area | Current files |
| --- | --- |
| Login/account queries | `source/LoginServer/LoginDatabase.cpp` |
| World data queries | `source/WorldServer/WorldDatabase.cpp` |
| Shared DB core | `source/common/database.*`, `source/common/dbcore.*`, `source/common/DatabaseNew.*`, `source/common/DatabaseResult.*` |
| Feature-specific world queries | `source/WorldServer/**/*DB.cpp` |

## Critical Login Tables Found

| Table | Current use |
| --- | --- |
| `account` | Account authentication, account creation, account IP, last client version |
| `login_worldservers` | World account validation, disabled state, world metadata, world IP/version updates |
| `login_characters` | Character select list, login-side character metadata, delete/level/race/class/gender/zone/name updates |
| `login_equipment` | Character select appearance equipment |
| `login_char_colors` | Character select appearance colors |
| `login_bannedips` | Login/world account IP ban checks |
| `login_versions` | Permitted world/login version checks |
| `login_worldstats` | World status, player count, zone count, max level, connected time |
| `login_config` | Login configuration such as max characters per account |
| `login_table_versions`, `download_tables` | Login-side table update/download support |
| `ls_world_zones` | Login-side zone description override data |
| `ls_character_picture` | Character picture upload/storage |
| `log_messages` | Client crash/verify/alert/base log storage |

## Critical World Tables Found

| Table or group | Current use |
| --- | --- |
| `characters` | Character load, current zone, position, class/race/gender, admin status, deletion |
| `character_details` | Stats, HP/power, coin, bind, AA, XP, biography, flags, language, pet name |
| `character_quest_rewards` | Quest reward state loaded during character bootstrap |
| `zones` | Zone metadata, safe points, requirements, instance type, script, ruleset, flags |
| `instances` | Instance player-level data and zone instance state |
| Spawn tables | NPC/object/sign/widget/ground spawn loading, spawn locations, spawn groups |
| Script tables | Spawn, zone, player, spell script data |
| Item tables | Item list, buyback, inventory, merchant inventory, broker data |
| Spell tables | Spell definitions, spell errors, spell classes, spell effects, custom spell data |
| Quest tables | Quest definitions, quest details, player quest state |
| Guild, broker, housing, achievements, collections | Feature-specific world repositories |

## Initial Repository Candidates

| Repository | Responsibility |
| --- | --- |
| `AccountRepository` | Login account lookup, account creation, status, auth metadata |
| `WorldServerRepository` | Registered world servers and world status |
| `CharacterRepository` | Character list, character select validation, character bootstrap |
| `ZoneRepository` | Zone metadata, safe points, spawn bootstrap |
| `ItemRepository` | Item definitions and inventory persistence |
| `QuestRepository` | Quest definitions and player quest state |
| `SpellRepository` | Spell definitions and spell state |

Additional candidates after inventory:

| Repository | Responsibility |
| --- | --- |
| `WorldStatusRepository` | World registration, status updates, world stats |
| `ZoneBootstrapRepository` | Zone metadata, instances, spawn bootstrap, revive points, location grids |
| `ScriptMetadataRepository` | Spawn/zone/player/spell script filename metadata |
| `StaticDataRepository` | Static startup data such as races, languages, titles, skills, rules |
| `BrokerRepository` | Broker sellers/items and sale state |
| `HousingRepository` | House zone data, player houses, house spawn instances |

## Async Query Risks

Known async DB worker creation exists in `source/common/database.cpp`. This needs a deeper trace before migration.

Risks:

- Query result ownership across detached threads.
- Callback execution thread is unclear.
- Query state may be protected by custom mutex containers.
- Hot-path synchronous queries may block game-state owner threads.
- `Client::HandleNewLogin` checks `database.IsActiveQuery(charID)` and delays login when async character save is active.
- Threaded world startup creates separate `WorldDatabase` instances for item and spell loading.
- Some zone loading calls run inside the zone processing path and can block zone startup.

## Source2 Implications

- Source2 should hide raw MariaDB APIs behind `eq2::db`.
- Blocking queries should run in a DB worker pool.
- Results should be posted back to the owning login/world/zone executor.
- Repositories should be grouped by ownership and flow, not copied one-to-one from legacy classes.
- Character login should be split into admission, character bootstrap, and zone bootstrap repositories.
- Login DB and world DB should remain separate connection configurations even if they share implementation code.
- Source2 should treat world startup static data as immutable snapshots where possible.

## Remaining Review Questions

- List login tables used by `LoginDatabase`.
- List world tables required for login-to-character-select.
- List zone bootstrap tables.
- Identify feature-specific `*DB.cpp` query ownership.
- Identify database calls made while locks are held.
- Decide fake DB/test DB strategy for phase 2.

The critical tables and ownership boundaries are now identified. Detailed SQL-by-SQL cataloging should happen when each repository is migrated.

## Phase 1 Database Exit Result

Database inventory is sufficient for phase 2 planning. Source2 should start with repositories for account/login, world registration/status, character select, character bootstrap, and zone bootstrap before migrating feature-specific systems.
