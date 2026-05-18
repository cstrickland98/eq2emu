# Phase 9: Full Live Acceptance Gate

Status: blocked.

## Purpose

Prove source2 login-server parity end to end and decide whether the legacy login
server can be retired for the covered deployment.

## Where Work Begins

Start after Phases 0 through 8 are complete.

## Work Entailed

- Run full automated source2 CI.
- Run DB-backed login smoke against the target login DB.
- Run source2 serve/probe with expected world count.
- Run a real EQ2 client against source2 on the same machine and over LAN.
- Run a real or characterized world server registration against source2.
- Exercise login accept/reject/bad version/account creation if enabled.
- Exercise world list and character list.
- Exercise create/delete/play against a test world where available.
- Compare source2 logs and packet traces against original behavior.
- Document any approved differences.
- Decide whether legacy login can be retired, kept as fallback, or requires a
  further migration.

## Deliverables

- Acceptance report in this phase under `Progress`.
- Final source2 login-server guide updates.
- Legacy retirement recommendation for login only.
- Known-issues list for any approved differences.

## Exit Criteria

- The real client can complete login through source2 in every scenario covered
  by the original login server for the target deployment.
- Automated tests and live gates pass.
- No unreviewed original login-server scenario remains unsupported.
- Operators have documented commands to build, configure, run, diagnose, and
  recover source2 login.

## Progress

- Full source2 CI after the persistent world-registration and target-probe
  fixes:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

Observed result on 2026-05-16 after adding UDP `OP_Combined` outbound
coalescing parity, combined-reply probe/smoke decoding, the real-client
acceptance-session, client-config, client-sandbox, acceptance result recorder,
combined gate, acceptance evidence audit, client snapshot secret-leakage
rejection, required localhost/LAN evidence rejection, migration-level
forbidden-secret pass-through, final `login_accepted` audit enforcement, Git
metadata diagnostics, guarded phase-commit helper, client auto-login dry-run
redaction coverage, client session-argument redaction coverage, DirectX/window
preflight helper, packet-capture helper, and migration completion audit
helpers: source2 CTest reported 0 failures out of 155 tests; the opt-in target
live-login CTest was skipped by design.

- Target DB-backed live gate with source2 world registration:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1 -NoTranscript
```

Observed result on 2026-05-15: the CTest target live-login gate passed, including
the target DB checks, MariaDB-backed smoke, temporary source2 login/world serve,
and source2 probe expecting one world.

Rerun after the client-sandbox helper and wrapper validation updates also
passed on 2026-05-15: 4/4 matching target CTest tests passed, including the
opt-in `source2_target_live_login_ctest`.

Rerun on 2026-05-16 after adding UDP `OP_Combined` outbound coalescing parity
and combined-reply probe decoding also passed: 4/4 matching target CTest tests
passed, including the opt-in target live-login CTest with one registered
source2 world advertised at `192.168.1.41`.

- Direct target verifier output:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1 -LoginPort 9100 -WorldPort 9101
```

Observed result on 2026-05-15:

```text
login-db-check reply_code=0 account_id=1 ... key_request_opcode=2
world-db-check ok database=eq2emu
smoke-login-mariadb reply_code=0 account_id=1 world_list=yes worlds=0 character_list=yes post_world_reply=yes
login probe reply_code=0 account_id=1 world_list=yes worlds=1 character_list=yes post_world_reply=yes
source2 live login verification passed.
```

- Final guide recovery coverage:
  - `docs/source2_login_server_guide.md` now includes a recovery/rollback
    section covering how to stop source2, point the client back to the original
    login host, verify the legacy path, and review test-created login/world
    rows before retesting.
- Packet and diagnostic comparison:
  - `migrationv3/packet_diagnostic_comparison.md` records the pre-fix
    source2/original packet divergence and the automated post-fix source2
    verifier evidence. It remains partial because a fresh post-fix real-client
    capture has not been collected.
- Failed-login diagnostic gate:
  - ran source2 login with `--diagnostic-events`,
  - probed with the target account name and an intentionally wrong password,
  - observed `reply_code=1`, `login_rejected`, `login_failures=1`, and no
    password value in the diagnostic output.
- LAN-address source2 probe:
  - started temporary source2 login on `0.0.0.0:9100`,
  - started temporary source2 world registration against that login server,
  - probed the login server through `192.168.1.41:9100`,
  - observed `reply_code=0`, `world_list=yes`, `worlds=1`,
    `character_list=yes`, and `post_world_reply=yes`.
  - artifacts were written under `artifacts\source2-lan-probe-gate`.
- Known issues and retirement recommendation:
  - `migrationv3/phase_9_known_issues_and_recommendation.md` recommends
    keeping the original login server as fallback until post-fix real-client
    localhost and LAN gates pass.
- Real-client acceptance checklist:
  - `migrationv3/real_client_acceptance_report.md` records the exact manual
    localhost, LAN, character lifecycle, failed-login, and fresh packet/log
    evidence still required to close this phase.
  - `scripts/source2_run_real_client_acceptance_session.ps1` starts source2
    login/world with diagnostic logs and writes a timestamped `session.md` for
    those manual gates.
  - A bounded localhost preflight run generated
    `artifacts\source2-real-client-acceptance\20260515-212620\session.md`; the
    login diagnostics recorded `world_registered` and `registered_worlds=1`.
    This validates the acceptance wrapper but does not replace the real-client
    UI gate.
  - A bounded LAN-target preflight run generated
    `artifacts\source2-real-client-acceptance\20260515-212814\session.md` with
    `cl_ls_address 192.168.1.41`; login diagnostics again recorded
    `world_registered` and `registered_worlds=1`.
  - `scripts/source2_set_eq2_client_login.ps1` provides a dry-run and
    backup/restore path for switching the local client `eq2_default.ini`
    between source2 and the original login host during manual gates.
  - CTest `source2_set_eq2_client_login_backup_restore` covers update and
    restore behavior against a temporary config file without touching the real
    client install.
  - `scripts/source2_run_real_client_acceptance_session.ps1` can also launch
    the local client and restore its config when run with `-UpdateClientConfig
    -LaunchClient -RestoreClientConfigOnExit`; it also handles an empty client
    argument list and offers an optional `-AutoLoginClient` path for
    interactive desktops where sending login keys is allowed.
  - The acceptance-session helper records redacted client launch arguments in
    new `session.md` files so command-line autologin attempts can be audited
    without exposing sensitive values.
  - The wrapper writes a failed-before-ready `session.md` if startup fails
    before the manual gate can begin, so incomplete attempts have an auditable
    reason instead of loose logs only.
  - The wrapper also supports `-SnapshotClientLogs` to copy common client-side
    logs, including `eq2_packet_log.jsonl` when present, into the session
    artifact directory.
  - A workspace-local sandbox client was created at
    `artifacts\eq2-client-sandbox` to avoid editing the installed client
    config. Earlier launches from this shell produced `D3DERR_NOTAVAILABLE`;
    later bounded launches sent auto-login keys or session-token/config
    autologin hints but source2 diagnostics still stopped at
    `session_requested` with `login_attempts=0`, so they did not produce
    acceptable real-client login evidence.
  - Post-coalescing sandbox run
    `artifacts\source2-real-client-acceptance\20260516-051841\session.md`
    used the rebuilt UDP `OP_Combined` coalescing source2 binaries, registered
    `Source2World`, and sent username/password auto-login keys; source2 still
    logged `session_requested` followed by disconnect with `login_attempts=0`.
  - Post-coalescing native SendInput sandbox run
    `artifacts\source2-real-client-acceptance\20260516-052407\session.md`
    used the same rebuilt source2 binaries plus explicit window-relative
    login-scene coordinates from `LSUsernamePassword.WindowPage`; source2 again
    logged `session_requested`, then disconnect, with `login_attempts=0`,
    `malformed_packets=0`, and `unsupported_packets=0`.
  - Client-area and virtual-key SendInput sandbox runs
    `artifacts\source2-real-client-acceptance\20260516-053204\session.md`,
    `20260516-053742\session.md`, and `20260516-053916\session.md`
    recorded the EQ2 window and client-area geometry in
    `auto-login-input.txt`. The window was `1040x807`, the client area was
    `1024x768`, and the client origin was offset from the outer window by
    `8,31`, confirming that XML UI coordinates must use client-area mode. The
    corrected client-area, virtual-key, and delayed virtual-key attempts still
    stopped at `session_requested`/`login_attempts=0`.
  - Scan-code SendInput sandbox run
    `artifacts\source2-real-client-acceptance\20260516-054935\session.md`
    used client-area field clicks, scan-code username/password text, and
    scan-code Enter for submit; source2 still stopped at
    `session_requested`/`login_attempts=0`.
  - Visual capture probes
    `artifacts\source2-real-client-acceptance\20260516-055439` and
    `artifacts\source2-real-client-acceptance\20260516-055621` confirmed
    this shell cannot inspect the DXVK-rendered client surface: `PrintWindow`
    returned a frame with a blank white Direct3D client area, while `BitBlt`
    returned false and a black bitmap.
  - Sandbox-only focus-hint run
    `artifacts\source2-real-client-acceptance\20260516-060043\session.md`
    temporarily added `OnShow="set_focus WindowPage.Username.FullPath"` to the
    sandbox login XML, then restored the file. Keyboard-only scan-code input
    still stopped at `session_requested` followed by disconnect with
    `login_attempts=0`.
  - Strengthened-focus SendInput run
    `artifacts\source2-real-client-acceptance\20260516-060658\session.md`
    added foreground diagnostics and `AttachThreadInput` around the focus
    attempt. The EQ2 window handle was valid, but `GetForegroundWindow`
    remained `0x0` and `SetForegroundWindow` returned false in this shell; the
    client still stopped at `session_requested`/`login_attempts=0`.
  - Direct `WindowMessage` runs
    `artifacts\source2-real-client-acceptance\20260516-060952\session.md` and
    `artifacts\source2-real-client-acceptance\20260516-061126\session.md`
    posted activation, focus, mouse, character, and Enter/Login-button messages
    directly to the EQ2 top-level window. The posted messages returned success,
    but the real client still did not submit credentials.
  - Five-minute manual-interaction window
    `artifacts\source2-real-client-acceptance\20260516-061607\session.md`
    launched source2 login/world and the sandbox client without auto-login so
    the client could be driven manually from the desktop. The resulting source2
    log still stopped at `session_requested` followed by disconnect with
    `login_attempts=0`.
  - Bounded installed-client runs generated
    `artifacts\source2-real-client-acceptance\20260516-012315` and
    `artifacts\source2-real-client-acceptance\20260516-012745`; after adding
    the legacy session-disconnect reply, a rebuilt installed-client run
    generated `artifacts\source2-real-client-acceptance\20260516-013523`.
    The post-patch run recorded a disconnect event, but source2 still stopped
    at `session_requested` with `login_attempts=0`, and the copied client
    packet/alert logs were stale from 2026-05-15. These are not accepted as
    Phase 9 real-client evidence.
  - Exact replay of the real-client source2 UDP payloads reproduced the next
    source2 blocker as `malformed login request payload` in
    `artifacts\source2-captured-client-replay\20260516-015438`.
    After correcting classic `LS_LoginRequest` parsing, replay
    `artifacts\source2-captured-client-replay\20260516-015711` logged
    `login_accepted`, `login_attempts=1`, `malformed_packets=0`, and an
    encrypted sequenced login reply.
  - A post-parser-fix installed-client bounded run generated
    `artifacts\source2-real-client-acceptance\20260516-015831`, but auto-login
    key injection from this non-interactive shell still did not submit a login
    request. The source2 logs stopped at `session_requested` with
    `login_attempts=0`, and client-side logs remained stale. This is not
    accepted as Phase 9 real-client evidence.
  - A longer post-parser-fix installed-client run generated
    `artifacts\source2-real-client-acceptance\20260516-021619` with
    username/password auto-login key entry after a longer delay. Source2 still
    stopped at `session_requested` with `login_attempts=0`, and the copied
    client-side logs remained stale from 2026-05-15. This is not accepted as
    Phase 9 real-client evidence.
  - A command-line `cl_autologin` installed-client run generated
    `artifacts\source2-real-client-acceptance\20260516-022448`. Source2 still
    stopped at `session_requested` with `login_attempts=0`, and the copied
    client-side logs remained stale from 2026-05-15. This is not accepted as
    Phase 9 real-client evidence.
  - A console-style `+cl_autologin` installed-client run generated
    `artifacts\source2-real-client-acceptance\20260516-023406`. The session
    report recorded redacted client launch arguments. Source2 still stopped at
    `session_requested` with `login_attempts=0`, and the copied client-side
    logs remained stale from 2026-05-15. This is not accepted as Phase 9
    real-client evidence.
  - Client binary string inspection found `cl_autologin`,
    `cl_autoplay_allowed`, `cl_autoplay_world`, `cl_autoplay_char`,
    `cl_sessionid`, and `cl_username`. The original login server source still
    authenticates login requests by username/password, so the session-token
    path is not accepted as the missing source2 parity blocker.
  - `scripts/source2_prepare_eq2_client_sandbox.ps1` now makes that sandbox
    setup repeatable for an interactive desktop with DirectX available and
    writes `source2_sandbox_manifest.tsv` for audit.
  - `scripts/source2_audit_real_client_acceptance_session.ps1` validates a
    completed session folder after the manual client run, including manual
    pass/fail rows, source2 log paths, a required `login_accepted` source2
    event, world registration diagnostics, client-log snapshots, and
    failed-login diagnostics.
  - `scripts/source2_record_real_client_acceptance_results.ps1` updates the
    generated `session.md` manual gate table from explicit command-line
    results and can reject sessions that still have pending results.
  - `scripts/source2_run_real_client_acceptance_gate.ps1` sequences localhost
    and LAN acceptance-session commands, result recording, and evidence audit
    commands for the remaining manual gate.
  - `scripts/source2_capture_login_packets.ps1` gives operators a repeatable
    elevated `pktmon` capture path for UDP 9100 traces when Wireshark/tshark is
    not already being used.
  - `scripts/source2_check_eq2_client_directx.ps1` launches the configured
    EQ2 client briefly and captures the top-level window text so operators can
    detect `D3DERR_NOTAVAILABLE` before starting a full acceptance run.
  - `scripts/source2_audit_migrationv3_completion.ps1` performs the final
    migration-level audit for unblocked phase statuses, required phase commits,
    and real-client evidence.
  - `scripts/source2_diagnose_git_permissions.ps1` performs a repeatable,
    non-destructive Git metadata write/ACL diagnostic for the current
    `.git/index.lock` permission blocker; CTest
    `source2_diagnose_git_permissions_writable_temp_repo` covers the helper
    against a temporary writable repository.
  - `scripts/source2_commit_migrationv3_phases.ps1` prints or runs the
    path-based phase commit sequence after `.git` permissions are fixed; Phase
    9 is explicitly guarded by `-IncludePhase9`.

Acceptance report status:

| Gate | Evidence | Status |
| --- | --- | --- |
| Full automated source2 CTest | 0 failures out of 155 tests on 2026-05-16 after UDP `OP_Combined` outbound coalescing parity and combined-reply probe/smoke decoding. | Passed |
| Target DB-backed live gate | `source2_run_target_live_login_gate.ps1 -RunWorldServe -ExpectedWorldCount 1 -WorldAdvertisedAddress 192.168.1.41 -NoTranscript` passed on 2026-05-16 after the UDP coalescing change; 4/4 matching target CTest tests passed, including the opt-in live gate. | Passed |
| Source2 serve/probe expected world count | Direct verifier reported `worlds=1` and `source2 live login verification passed`. | Passed |
| Real client on source2 localhost | Exact captured UDP replay now reaches `login_accepted`, but bounded sandbox-client and installed-client UI attempts on 2026-05-16 launched source2 login/world and the client without producing an accepted real-client login from the UI. DXVK removed the sandbox client's DirectX fatal dialog; `artifacts\source2-real-client-acceptance\20260516-035531\session.md` registered `Source2World`, later `20260516-043439`/`20260516-043658` runs tested corrected nested login-scene coordinates plus direct Win32 window messages, diagnostic-only run `20260516-044357` temporarily biased the sandbox login XML toward submission, post-coalescing run `20260516-051841` used the rebuilt UDP `OP_Combined` coalescing source2 binaries, post-coalescing SendInput run `20260516-052407` used explicit field/button coordinates, `20260516-053204`/`20260516-053742`/`20260516-053916` tested client-area coordinates plus virtual-key text injection, `20260516-054935` tested scan-code text and Enter submit, `20260516-055439`/`20260516-055621` confirmed GDI capture cannot see the DXVK client surface, `20260516-060043` tested a temporary sandbox XML focus hint plus keyboard-only scan-code input, `20260516-060658` showed foreground activation fails from this shell, `20260516-060952`/`20260516-061126` showed direct posted window messages also do not submit credentials, and `20260516-061607` provided a manual-interaction window that still produced no accepted login. Diagnostics still stopped at `session_requested`/`login_attempts=0`. | Blocked |
| Real client over LAN | Automated source2 probe through `192.168.1.41:9100` passed with one world; command-line `+cl_ls_address 192.168.1.41` run `artifacts\source2-real-client-acceptance\20260516-030148` still stopped at `session_requested`/`login_attempts=0`. The current shell still cannot complete the real-client UI/input gate; with DXVK the blocker moved from client launch to credential submission. | Partially passed |
| World registration visible to client | Automated source2 probe sees one world; real-client visibility not run in this shell. | Partially passed |
| Character list/create/delete/play | Automated source2 probe sees a character-list reply; real-client create/delete/play not run in this shell. | Partially passed |
| Source2/original packet or diagnostic comparison | `packet_diagnostic_comparison.md` records the pre-fix packet divergence, exact captured source2 replay failure before the parser fix, exact captured source2 replay success after the parser fix, and the 2026-05-16 UDP outbound coalescing parity follow-up. Source2 now attempts legacy `OP_Combined` coalescing for small CRC-protected UDP response batches, and the target DB-backed login/world-list gate passes through the combined-reply decoder path. Fresh accepted UI/client capture is still pending. | Partially passed |
| Failed-login diagnostics | Automated diagnostic gate emitted `login_rejected` and `login_failures=1` without logging the bad password; real-client failed-login check still pending. | Partially passed |
| Known issues / approved differences | `phase_9_known_issues_and_recommendation.md` lists no approved behavior differences and records the open validation gaps. | Passed |
| Legacy-login retirement recommendation | Interim recommendation is to keep the original login server as fallback until real-client gates pass. | Passed |

Remaining before completion:

- Run the manual real-client gate on localhost and LAN.
- Run the manual real-client gate from an interactive desktop environment with
  DirectX available; the current shell cannot render the client UI.
- Fill in `migrationv3/real_client_acceptance_report.md` with the post-fix
  real-client results and artifact paths.
- Run `source2_audit_real_client_acceptance_session.ps1` with
  `-RequireLoginAccepted` against the completed localhost/LAN session folders
  before accepting the gate.
- Use `source2_record_real_client_acceptance_results.ps1` to record manual gate
  results before running the evidence audit.
- Prefer `source2_run_real_client_acceptance_gate.ps1 -DryRun` before the
  manual run to print the exact localhost/LAN session, record, and audit
  commands.
- Run `source2_audit_migrationv3_completion.ps1` without `-AllowKnownBlockers`
  before marking migrationv3 complete.
- Record fresh post-fix source2/original real-client packet or diagnostic
  comparisons for any approved differences, including whether the client
  observes source2's new legacy `OP_Combined` UDP response batching.
- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1
```

Manual live gate:

```text
1. Start source2 login with the target config.
2. Start/register a world server.
3. Point the real EQ2 client to source2.
4. Log in, view worlds, view characters, and play a test character.
5. Repeat from LAN using the source2 host address.
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 scripts docs migrationv3
git commit -m "Accept source2 login server parity"
```
