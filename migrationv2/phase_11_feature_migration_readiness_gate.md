# Phase 11: Feature Migration Readiness Gate

Status: blocked.

## Purpose

Decide whether source2 is ready for gameplay feature migration. This phase is a
hard gate: if live architecture evidence is incomplete, feature migration
should not begin.

The local architecture gate is implemented and green, but the requested
deployment target is not fully proven because TCP `3306` is not reachable
between this source2 machine and MariaDB at `192.168.1.243`. Treat feature
migration as conditional until the target DB-backed live verifier passes.

## Where Work Begins

Start after Phases 1 through 10 are complete or explicitly waived. Use all phase progress notes, tests, smoke logs, and documented compatibility gaps.

## Work Entailed

- Audit the platform against the original AzerothCore-derived goals:
  - target-based build,
  - tests,
  - config policy,
  - logging policy,
  - DB lifecycle,
  - live transport,
  - repository boundaries,
  - scripting boundary,
  - CI/tools,
  - owner-executor model.
- Run full source2 build and tests.
- Run live smoke tests:
  - login,
  - world registration,
  - character list/select,
  - zone admission,
  - Lua script call if Phase 9 is complete.
- Review compatibility gaps and decide whether they block feature work.
- Select the first feature migration pilot.
- Define feature migration rules:
  - required characterization tests,
  - allowed dependencies,
  - DB migration requirements,
  - Lua API requirements,
  - smoke test requirements.

## Deliverables

- Readiness audit recorded in this file.
- Explicit go/no-go decision for gameplay feature migration.
- Selected first feature migration pilot.
- Feature migration checklist.
- Known risks and rollback strategy.

## Exit Criteria

- Full source2 build and tests pass.
- Required live smoke tests pass or are explicitly waived with accepted risk.
- No fake-only runtime path is used as evidence for live readiness.
- First feature pilot is selected with clear scope and tests.
- Project owner accepts the readiness decision.

## Progress

- Audited source2 against the AzerothCore-derived platform goals:
  - target-based build: complete,
  - tests: complete with CTest coverage across core/protocol/net/db/scripting/zone/login/world,
  - config policy: typed runtime validation exists for app composition,
  - logging policy: source2 log sink and app startup logs exist,
  - DB lifecycle: migration layout and updater exist,
  - live transport: TCP/UDP loopback transport exists,
  - repository boundaries: SQL repositories exist for login/world/character/zone boundaries,
  - scripting boundary: real Lua backend exists with a strict source2-safe API subset,
  - CI/tools: local CI script, GitHub workflow, and first-class tool target exist,
  - owner model: zone command owner and world/zone handoff boundaries exist.
- Ran normal local CI:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1`
  - Result: superseded by the current vcpkg-backed source2 CI run.
- Ran live-smoke local CI:
  - `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify`
  - Result: 110 registered tests, 109 executed/passed, and 1 skipped opt-in
    target live login gate. Executed tests include login/world live smoke, target config
    parse tests, env-over-config/help-output tests, PowerShell owner wrapper tests,
    owner start/probe dry-run secret-contract/config-secret/env-clearing/skip-preflight/skip-account-preflight-contract/command-quoting/call-operator/execution-policy/gate-label/env-scope tests,
    one-command target live gate dry-run/TCP-before-secret/env-propagation
    tests, target live CTest env-override tests, exit-after-env-restore tests,
    start/probe-wrapper TCP-before-secret coverage, live verifier login/world
    env-scope tests, opt-in target live CTest parse/default-skip coverage, and source2
    loopback login/world tests.
- Recorded feature migration rules in `docs/source2_feature_migration_rules.md`.
- Selected the first feature migration pilot:
  - `Zone safe-location/player-admission feature slice`.
- Follow-up blocker closure:
  - MariaDB C API adapter and CMake discovery path added; vcpkg `libmariadb`
    now enables the adapter and deploys runtime DLLs beside the owner-facing
    `eq2_login_server.exe` and `eq2_world_server.exe` executables.
  - DB updater now has `--mariadb` mode with CLI/env connection configuration.
  - TCP response writes are implemented and tested through net/login/world.
  - Login reply/world list, login-to-world handoff, play-character, and zone snapshot/update serializers are implemented for current source2 paths.
  - `GetCurrentZoneSafeLocation` is implemented for the zone admission Lua feature path.
  - Direct executable checks pass:
    `eq2_login_server.exe --smoke-login-live --username <login-user>
    --password <login-password> --client-version 546 --expect-world-count 0`
    and `eq2_world_server.exe --smoke-world-live`.

## Decision

Conditional go for feature migration inside the implemented source2 boundaries.

No-go for broad live-client parity yet.

No-go for claiming target live login readiness until
`scripts/source2_live_login_verify.ps1` passes against `192.168.1.243` using the
`eq2ls` login DB, `eq2emu` world DB, and the `testlabs` login account with the
configured login password.

Remaining broad-parity gaps:

- The source tree builds and runs the MariaDB C API adapter through vcpkg
  `libmariadb`; live MariaDB validation still needs TCP reachability to the
  target DB host and exposure of the required login/world schemas.
- Full legacy live-client opcode/packet parity is not complete. LoginStream opcode width is configurable and source2 can load the login request/reply/world-list opcode values from the login DB `opcodes` table, but current serializers still cover only the implemented source2 paths and must expand feature by feature.
- Legacy Lua API compatibility is intentionally limited to the source2-safe subset plus feature-driven additions such as `GetCurrentZoneSafeLocation`.

The selected Phase 12 pilot is allowed because it stays inside the proven boundaries: DB-backed zone metadata, world-to-zone handoff, zone owner commands, snapshots, and Lua-safe hooks.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Verification run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```
