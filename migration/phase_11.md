# Phase 11: Gameplay Systems

Status: complete.

## Purpose

Migrate gameplay behavior in small, reviewable groups after the source2 runtime boundaries are stable.

## Scope

Potential migration groups:

- Spawn lifecycle.
- Inventory.
- Items.
- Quests.
- Spells.
- Combat.
- Tradeskills.
- Guilds.
- Broker.
- Housing.
- Achievements.
- Collections.

## Deliverables

Each gameplay migration should include:

- Current behavior notes.
- Minimal tests.
- Interface placement.
- Thread ownership decision.
- Legacy compatibility notes.

## Progress

- Added `eq2::world::GameplayMigration` registry for the planned gameplay migration groups.
- Recorded source2 owner decisions, interface placement, current legacy behavior notes, verification notes, and explicit compatibility gaps for:
  - Spawn lifecycle.
  - Inventory.
  - Items.
  - Quests.
  - Spells.
  - Combat.
  - Tradeskills.
  - Guilds.
  - Broker.
  - Housing.
  - Achievements.
  - Collections.
- Added `eq2_gameplay_systems_tests` to verify that every planned group has an owner, interface placement, current behavior note, compatibility note, and verification note.
- The initial migrated behavior remains the Phase 9 spawn/movement/combat command boundary and the Phase 10 script event boundary; detailed parity for item, quest, spell, tradeskill, guild, broker, housing, achievement, and collection rules is explicitly recorded as remaining legacy behavior.

## Source2 Owner Decisions

| Gameplay group | Source2 owner | Interface placement | Compatibility gap |
| --- | --- | --- | --- |
| Spawn lifecycle | Zone executor | `eq2::zone` | Detailed spawn serialization remains legacy. |
| Inventory | Player session | `eq2::world` | Bag rules and persistence remain legacy. |
| Items | Player session | `eq2::world` | Detailed item stat/equip behavior remains legacy. |
| Quests | Player session | `eq2::world` | Quest journal and reward parity remain legacy. |
| Spells | Zone executor | `eq2::zone` | Spell formulas/effects remain legacy. |
| Combat | Zone executor | `eq2::zone` | Detailed combat formulas remain legacy. |
| Tradeskills | Player session | `eq2::world` | Recipe progression and crafting events remain legacy. |
| Guilds | World service | `eq2::world` | Roster, ranks, and persistence remain legacy. |
| Broker | World service | `eq2::world` | Search, sale, and escrow behavior remain legacy. |
| Housing | World service | `eq2::world` | Access and instance bootstrap remain legacy. |
| Achievements | Player session | `eq2::world` | Criteria and rewards remain legacy. |
| Collections | Player session | `eq2::world` | Completion behavior remains legacy. |

## Exit Criteria

- Each migrated gameplay system has a clear source2 owner.
- Behavior is covered by tests or documented smoke checks.
- Legacy compatibility gaps are explicit.
