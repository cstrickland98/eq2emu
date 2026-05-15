# Phase 8: World Session Slice

Status: complete.

## Purpose

Rebuild the first world-server flow after login succeeds.

## Scope

Focus on world registration, client session creation, character list, character select, and the initial boundary for zone handoff.

## Deliverables

- Source2 world app wiring.
- World server registration flow.
- Client session model.
- Character list retrieval.
- Character select handling.
- Zone handoff interface.

## Progress

- Added `eq2::world::WorldServerConfig` and config loading from `eq2::core::ConfigProvider`.
- Added protocol-owned `encode_server_ls_info_payload` and used it to build source2 world `ServerOP_LSInfo` registration frames.
- Added `eq2::world::WorldServer`, `WorldClientSession`, character summary/select results, and an explicit `ZoneHandoff` interface.
- Character-list and character-select behavior now reads through `eq2::db::CharacterListRepository`.
- Zone entry is represented as a handoff request posted through an interface; world does not include or mutate zone internals.
- Updated the `eq2_world_server` app target to load source2 world config and report the configured world/login endpoints.
- Added `eq2_world_server --smoke-world`, which runs the source2 world app target through character selection and zone handoff using source2 DB and handoff boundaries.
- Added `eq2_world_session_tests` covering config loading, world registration framing, known-account character-list/select behavior, and zone handoff interface usage.

## Exit Criteria

- Login-to-world flow is functional.
- Character list behavior matches legacy behavior for known accounts.
- World code does not directly own zone internals.
