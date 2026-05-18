# Migration V3: Login Server Legacy Parity

Status: blocked.

## Purpose

Migration v3 brings the source2 login server to full behavioral parity with the
original `source/LoginServer` login server before any legacy login-server path
is retired.

The immediate trigger for this migration is that the source2 probe can pass
while the real EQ2 client still stalls at `Trying login server #1`. Packet
captures show the client reaches source2, but source2 does not yet process the
same embedded encrypted LoginStream sequence that the original server handles.

## Scope

Parity means source2 must handle every login-server scenario that the original
login server handles, with evidence from characterization tests, packet
replays, live client checks, and DB-backed integration checks.

This includes:

- EQStream LoginStream session establishment, CRC, ACK, resend, combined,
  app-combined, fragment, keepalive, disconnect, reconnect, and out-of-order
  behavior.
- Legacy RSA/RC4 key negotiation and encrypted LoginStream application packet
  framing.
- Login request parsing for supported client-version ranges.
- Account authentication, account creation policy, duplicate-session behavior,
  account IP/client-version updates, bans, rejected login reasons, and bad
  version behavior.
- World-server TCP registration, authentication, disabled accounts, ghost world
  cleanup, world status, world-list generation, world stat persistence, zone
  update and equipment appearance update flows.
- Character select list loading/serialization, create-character request/response,
  delete-character request/response, play-character request/response, pending
  delete handling, and login-to-world handoff.
- Client crash/verify/alert/base log ingestion.
- Login config, opcode/struct loading, console operations, web status endpoint,
  logging, and operational diagnostics.

## Status Values

Use these values in each phase file:

- `planned`: no implementation work has started.
- `in progress`: implementation or verification is underway.
- `blocked`: work cannot continue without a dependency or decision.
- `complete`: all exit criteria are met with concrete evidence.

Do not mark a phase complete because source2-only probes pass. A phase is
complete only when its legacy scenarios are covered by characterization evidence
and by the listed verification gate.

## Phase Order

| Phase | File | Goal |
| --- | --- | --- |
| 0 | `phase_0_baseline_packet_audit.md` | Establish the legacy/source2 packet and behavior baseline. |
| 1 | `phase_1_legacy_login_inventory.md` | Inventory every original login-server scenario and map it to source2 work. |
| 2 | `phase_2_loginstream_transport_parity.md` | Bring LoginStream protocol/session/reliability behavior to parity. |
| 3 | `phase_3_encrypted_login_handshake_parity.md` | Match legacy RSA/RC4 key negotiation and encrypted app framing. |
| 4 | `phase_4_account_login_policy_parity.md` | Match login request, account, version, ban, and duplicate-session behavior. |
| 5 | `phase_5_world_registration_list_parity.md` | Match world registration, world-list, status, stats, and update flows. |
| 6 | `phase_6_character_lifecycle_parity.md` | Match character list/create/delete/play and login-to-world handoff flows. |
| 7 | `phase_7_client_logs_admin_web_parity.md` | Match client log ingestion, console commands, web status, and maintenance jobs. |
| 8 | `phase_8_observability_and_diagnostics.md` | Add diagnostics needed to prove live-client parity without packet guessing. |
| 9 | `phase_9_full_live_acceptance_gate.md` | Run the final parity gate and decide legacy-login retirement readiness. |

## Review Gates

Pause for review after:

- Phase 1: legacy behavior inventory is accepted as complete.
- Phase 3: real client reaches encrypted login reply with packet parity evidence.
- Phase 5: world registration and world-list parity is accepted.
- Phase 6: character select/create/delete/play parity is accepted.
- Phase 9: full live-client parity is accepted.

## Working Rules

- Preserve the legacy source tree as the oracle until Phase 9 accepts parity.
- Prefer characterization and packet-replay tests before refactors.
- Do not broaden into world/zone gameplay behavior except where the original
  login server directly mediates login-to-world handoff.
- Every phase must end with a git commit after its exit criteria pass.
- Commit messages are listed in each phase file and should be used unless the
  implemented scope materially changes.
