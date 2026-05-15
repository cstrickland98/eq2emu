# Phase 12: Legacy Retirement

Status: complete.

## Purpose

Remove or freeze legacy code only after source2 reaches verified functional parity for a subsystem.

## Scope

Retirement happens per subsystem, not all at once.

## Deliverables

- Legacy subsystem freeze notes.
- Source2 parity checklist.
- Deployment documentation updates.
- Build script updates.
- Removal plan for obsolete source files.

## Progress

- Added `docs/source2_legacy_retirement.md`.
- Documented subsystem freeze notes for protocol, net, login, world session, zone runtime, scripting, and gameplay systems.
- Added a parity checklist that must be satisfied before any legacy subsystem is retired.
- Documented deployment guidance: legacy binaries remain production paths until live-client parity is accepted, while source2 binaries are migration smoke-test targets.
- Documented build-script policy for keeping `EQ2EMU_BUILD_SOURCE2` enabled and adding retirement switches only after subsystem parity is accepted.
- Added a per-subsystem removal plan and explicitly recorded that no legacy source files are removed by this phase.
- Confirmed source2 app smoke commands: `eq2_login_server --smoke-login` and `eq2_world_server --smoke-world`.

## Exit Criteria

- Source2 replacement is tested and smoke-tested.
- Deployment docs no longer require the retired legacy path.
- Legacy code is removed only after compatibility risk is accepted.
