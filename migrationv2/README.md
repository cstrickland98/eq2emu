# Migration V2: Platform Readiness Before Feature Migration

Status: blocked.

## Purpose

Migration v1 established source2 module boundaries and testable scaffolding. The follow-up review found that the architecture direction is sound, but source2 needed a second integration pass before feature migration could proceed safely.

Migration v2 is the architecture and integration pass required before gameplay feature migration begins.

The source2 architecture implementation is in place and the local vcpkg-backed
build/test path is green with 110 registered tests, 109 executed/passed, and
1 skipped opt-in target live login gate. Final live login readiness
is still blocked in this Codex shell by the
`codex_sandbox_offline_block_outbound` firewall rule, so the login/world DB
gates cannot yet run against `192.168.1.243`, `eq2ls`, and `eq2emu` here.

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
- DB-backed login opcode lookup from the legacy `opcodes` table for the source2 login server.
- Live TCP world registration feeding UDP login world-list responses.
- World DB configuration example and zone-bootstrap schema preflight for the source2 world server.
- DB-backed zone bootstrap and a first safe-location/player-admission pilot.
- TCP response writes for source2 live services.
- Login reply/world list, login-to-world handoff, play-character, and zone snapshot/update serialization for the implemented source2 paths.
- MariaDB connector discovery and adapter code, with vcpkg `libmariadb` support and explicit disabled-adapter behavior when the connector SDK is not installed.
- Legacy retirement documentation that keeps legacy production paths until live parity is accepted.

The current source2 tree still does not claim:

- Live MariaDB validation against the target DB host unless that host accepts TCP connections from the source2 machine and exposes the required login/world schemas.
- Full legacy live-client opcode and packet parity beyond the implemented source2 paths.
- Broad legacy Lua API compatibility.
- Broad gameplay parity beyond the Phase 12 pilot.

## Current Blocker

The local source2 build can download/link MariaDB Connector/C through vcpkg and
all source2 tests pass. The no-secret TCP helper and the live verifier both
show that the target host is reachable but TCP `3306` is blocked in the current
Codex shell, so the live verifier currently fails before source2 can
authenticate `testlabs`:

```text
Cannot reach 192.168.1.243:3306 from this host.
TCP failure detail: [LocalPolicy] An attempt was made to access a socket in a way forbidden by its access permissions 192.168.1.243:3306
```

`netsh advfirewall firewall show rule name=codex_sandbox_offline_block_outbound
verbose` confirms the current shell has an enabled `Codex Sandbox Offline -
Block Non-Loopback Outbound` rule. The final target-DB gate must run from a
normal, non-sandboxed PowerShell prompt or from a Codex session with network
policy relaxed for `192.168.1.243:3306`.

Follow `docs/source2_live_login_blocker_resolution.md` and rerun
`scripts/source2_check_mariadb_tcp.ps1`, then
`scripts/source2_live_login_verify.ps1` before changing this status to
`complete`.

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
- No-go for broad live-client gameplay parity until MariaDB is validated against a real connector/test DB and legacy client packet/API gaps are closed feature by feature.

## Non-Goals

- Do not port broad gameplay systems before Phase 11.
- Do not delete legacy production code in v2.
- Do not rely on fake repositories or in-memory transports as live parity evidence.
- Do not copy AzerothCore code directly without explicit license review.
- Do not redesign the EQ2 schema while adding migration tooling.
