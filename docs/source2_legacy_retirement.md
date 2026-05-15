# Source2 Legacy Retirement Gate

Legacy retirement is per subsystem. A subsystem is not retired just because a source2 boundary exists; it is retired only after parity is demonstrated and compatibility risk is accepted.

## Freeze Notes

| Subsystem | Current retirement state | Freeze rule |
| --- | --- | --- |
| Protocol helpers | Source2 boundary implemented and tested | New packet helpers should be added to `eq2::protocol`; legacy packet code remains available until live-client parity is accepted. |
| Net transport | Source2 abstraction implemented and tested | New source2 flows should use `eq2::net`; legacy `EQStream` stays until the source2 socket adapter has live smoke coverage. |
| Login flow | Source2 vertical slice implemented and tested with legacy byte fixtures | Legacy login server remains the production path until live socket/client compatibility is accepted. |
| World session | Source2 session and character-select boundary implemented and tested | Legacy world server remains the production path until login-to-world smoke tests pass against a live client. |
| Zone runtime | Source2 owner-executor boundary implemented and tested | Legacy zone simulation remains production until zone spawn/combat/update parity is accepted. |
| Scripting | Source2 scripting boundary implemented and tested | Legacy Lua engine remains production until Lua backend parity and API compatibility are accepted. |
| Gameplay systems | Source2 owners documented | Legacy gameplay rules remain production until each feature group has parity tests or accepted smoke coverage. |

## Parity Checklist

A subsystem can be retired only when all items are checked:

- Source2 implementation is covered by unit or characterization tests.
- A smoke test covers the same user-visible path as legacy.
- Database reads/writes are owned by source2 repositories or explicitly adapter-wrapped.
- Network/protocol bytes are generated or parsed through `eq2::protocol`.
- Mutable state is owned by a source2 executor or service boundary.
- Scripting calls are routed through `eq2::scripting` when script behavior is involved.
- Legacy compatibility gaps are documented and accepted.
- Rollback path is documented before deployment.

## Deployment Documentation Updates

Deployment docs should switch a subsystem from legacy to source2 only after the parity checklist is complete. Until then:

- Operators should continue using the legacy login/world binaries for production gameplay.
- Source2 binaries are migration smoke-test targets.
- `cmake -S . -B build/source2` and `cmake --build build/source2 --config Debug` remain the source2 build commands.
- `ctest --test-dir build/source2 -C Debug --output-on-failure` remains the source2 verification command.

## Build Script Updates

- Keep `EQ2EMU_BUILD_SOURCE2` enabled by default for migration verification.
- Add subsystem-specific retirement switches only when a source2 subsystem can replace a legacy path.
- Do not remove legacy build inputs until the corresponding retirement record is accepted.

## Removal Plan

For each legacy subsystem:

1. Record the source2 replacement and tests.
2. Run the source2 test suite and the subsystem smoke test.
3. Record compatibility gaps and acceptance.
4. Update deployment docs to use source2 for that subsystem.
5. Remove or freeze legacy files in a dedicated change.
6. Keep rollback instructions with the deployment notes for one release cycle.

## Current Retirement Decision

No legacy subsystem is removed in this phase. The source2 boundaries are tested migration artifacts, while the legacy runtime remains available until live-client parity is accepted subsystem by subsystem.
