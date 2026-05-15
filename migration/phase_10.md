# Phase 10: Lua Boundary

Status: complete.

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

## Progress

- Added `eq2::scripting::ScriptEngine` with load, reload, lookup, and event-call behavior.
- Added `ScriptBackend` so the eventual Lua implementation is hidden behind a testable source2 interface.
- Added `ScriptContext` and `OwnerCommandSink`; script-driven mutations are posted as owner commands instead of directly touching world or zone state.
- Added explicit event helper APIs for item, quest, spell, spawn, zone, player, and region events.
- Added script failure isolation: backend failures and exceptions become `eq2::core::Result` failures and are logged through `eq2::core::LogSink`.
- Added `eq2_scripting_tests` covering script lifetime/reload, owner-posted mutations, logged failures, exception isolation, and all explicit event categories.

## Exit Criteria

- Lua calls do not require holding world or zone locks.
- Script failures are isolated and logged.
- Script APIs are documented and testable.
