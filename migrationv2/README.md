# Migration V2: Platform Readiness Before Feature Migration

Status: complete.

## Purpose

Migration v1 established source2 module boundaries and testable scaffolding. The follow-up review found that the architecture direction is sound, but source2 is not yet ready for broad feature migration because key runtime pieces are still in-memory or fake-backed.

Migration v2 is the architecture and integration pass required before gameplay feature migration begins.

## Baseline

At the end of migration v2, the source2 tree has:

- Target-based CMake modules.
- Protocol helpers, stream pipeline tests, and characterization tests.
- Real loopback TCP/UDP transport boundaries.
- SQL repository implementations behind `QueryConnection`.
- Database migration layout and updater tooling.
- Production-shaped login/world app composition roots.
- Typed runtime config validation and console logging.
- Runtime-loaded Lua backend with a strict source2-safe API subset.
- Live smoke commands for source2 login/world using real loopback transport and SQL repository boundaries.
- DB-backed zone bootstrap and a first safe-location/player-admission pilot.
- Legacy retirement documentation that keeps legacy production paths until live parity is accepted.

The current source2 tree still does not yet have:

- Enabled MariaDB C/C++ connector support in the source2 build.
- Socket writes of source2 protocol responses back to connected clients.
- Full login/world/zone client packet response serialization.
- Broad legacy Lua API compatibility.
- Broad gameplay parity beyond the Phase 12 pilot.

## Status Values

Use these values in each phase file:

- `planned`: no implementation work has started.
- `in progress`: implementation or verification is underway.
- `blocked`: work cannot continue without a dependency or decision.
- `complete`: all exit criteria are met with concrete evidence.

Do not mark a phase complete because its tests pass unless those tests cover each exit criterion.

## Phase Order

| Phase | File | Goal |
| --- | --- | --- |
| 0 | `phase_0_baseline_audit.md` | Reconcile v1 claims with actual source2 state and define v2 gates. |
| 1 | `phase_1_app_composition_config_logging.md` | Make apps production-shaped composition roots with typed config and logging policy. |
| 2 | `phase_2_real_net_transport.md` | Replace in-memory net-only behavior with real TCP/UDP transport boundaries. |
| 3 | `phase_3_eqstream_protocol_pipeline.md` | Integrate EQ2 stream/session framing, transforms, and packet dispatch over net. |
| 4 | `phase_4_mariadb_repositories.md` | Add real DB connections and source2 repository implementations. |
| 5 | `phase_5_database_migrations_updater.md` | Add database migration layout and updater tooling. |
| 6 | `phase_6_login_live_vertical_slice.md` | Prove source2 login against real config, real transport, and real DB. |
| 7 | `phase_7_world_live_session_slice.md` | Prove source2 world registration, character list, select, and handoff. |
| 8 | `phase_8_zone_bootstrap_live_slice.md` | Bootstrap a real source2 zone runtime from DB data and serve minimal updates. |
| 9 | `phase_9_lua_backend_integration.md` | Attach a real Lua backend to the source2 scripting boundary. |
| 10 | `phase_10_ci_tools_packaging.md` | Add CI, tools, packaging conventions, and repeatable verification. |
| 11 | `phase_11_feature_migration_readiness_gate.md` | Decide whether source2 is ready for gameplay feature migration. |
| 12 | `phase_12_first_feature_migration_pilot.md` | Migrate the first narrow gameplay feature after readiness is proven. |

## Review Gates

Pause for review after:

- Phase 1: app/config/logging shape is accepted.
- Phase 3: live protocol transport shape is accepted.
- Phase 5: DB lifecycle shape is accepted.
- Phase 7: login-to-world live smoke path is accepted.
- Phase 9: scripting boundary with real Lua backend is accepted.
- Phase 11: feature migration readiness is accepted.

Phase 11 decision:

- Conditional go for the first narrow feature pilot.
- No-go for broad live-client gameplay migration until the remaining transport write, packet serialization, and live MariaDB gaps are closed.

## Non-Goals

- Do not port broad gameplay systems before Phase 11.
- Do not delete legacy production code in v2.
- Do not rely on fake repositories or in-memory transports as live parity evidence.
- Do not copy AzerothCore code directly without explicit license review.
- Do not redesign the EQ2 schema while adding migration tooling.
