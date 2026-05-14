# Phase 11: Gameplay Systems

Status: planned.

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

## Exit Criteria

- Each migrated gameplay system has a clear source2 owner.
- Behavior is covered by tests or documented smoke checks.
- Legacy compatibility gaps are explicit.

