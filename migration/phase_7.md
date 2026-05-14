# Phase 7: Login Vertical Slice

Status: planned.

## Purpose

Rebuild login first because it has clearer boundaries than world and zone simulation.

## Scope

Create a source2 login server path that uses source2 protocol, net, db, and core modules.

## Deliverables

- Login app wiring.
- Login server config loading.
- Account authentication through `eq2::db`.
- Login packet handling through `eq2::protocol`.
- Connection handling through `eq2::net`.
- World registration handling.

## Exit Criteria

- Existing client can reach the same login outcome as the legacy login server.
- Any temporary incompatibility with the legacy world server is explicitly documented.
- Login behavior is covered by characterization tests.

