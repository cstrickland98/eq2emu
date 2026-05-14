# Phase 10: Lua Boundary

Status: planned.

## Purpose

Wrap Lua behind a narrow, testable source2 scripting API.

## Scope

Scripting owns Lua engine lifetime, script loading, script calls, script context APIs, and error isolation. It should not expose raw world or zone internals.

## Deliverables

- Script engine lifetime management.
- Script loading and reload policy.
- Script context object.
- Explicit APIs for item, quest, spell, spawn, zone, player, and region events.
- Mutation operations that post back to the owning executor when required.

## Exit Criteria

- Lua calls do not require holding world or zone locks.
- Script failures are isolated and logged.
- Script APIs are documented and testable.

