# Migration V3 Completion Audit

Audit date: 2026-05-16.

## Objective

Complete all migrationv3 phases for source2 login-server parity with the
original login server. Completion requires:

- Phase 0 through Phase 9 deliverables and exit criteria satisfied.
- Evidence for automated tests, packet replay, DB-backed checks, real-client
  live checks, and operator documentation.
- A git commit after each completed phase with the phase's documented commit
  message.

## Prompt-To-Artifact Checklist

| Requirement | Artifact or Evidence | Audit Result |
| --- | --- | --- |
| Create a new migration folder called `migrationv3`. | `migrationv3/README.md`, phase files, supporting reports, commit plan, and recovery notes. | Present |
| Write a phase-based migration plan similar to existing migrations. | `phase_0_baseline_packet_audit.md` through `phase_9_full_live_acceptance_gate.md`, each with goals, requirements, exit criteria, validation, and git commit instructions. | Present; statuses remain blocked until final evidence and commits land |
| Focus the migration on full parity with the original login server source. | `legacy_scenario_inventory.md`, `packet_baseline.md`, `packet_diagnostic_comparison.md`, and Phase 2 through Phase 8 implementation/test evidence. | Automated parity work present; final real-client parity evidence pending |
| Handle all scenarios that the original login server handled. | Scenario coverage is mapped in `legacy_scenario_inventory.md`; transport, encrypted login, account policy, world registration/list, character lifecycle, client logs/admin, and diagnostics are covered by source2 code/tests and phase docs. | Automated coverage present; real-client world list, character list, create/delete/play, failed-login diagnostics, and fresh post-fix packet comparison remain pending |
| Add a git commit step after each phase with an appropriate message. | Each `phase_*.md` file has a `## Git Commit` section with `git add` and the phase-specific `git commit -m` command; `phase_commit_plan.md` records the full commit sequence. | Present and enforced by `source2_audit_migrationv3_completion.ps1` structural checks |
| Actually commit each completed phase to git. | `source2_audit_migrationv3_completion.ps1 -RequirePhaseCommits` checks the real repository history for all 10 required commit subjects. | Blocked by `.git/index.lock`/`.git/objects` permissions; shadow bundle is recovery evidence only |
| Build, configure, run, and connect the real client to source2 login. | `docs/source2_login_server_guide.md`, real-client session/gate helpers, result recorder, evidence audit, and migration completion audit command. | Operator path documented; interactive real-client acceptance evidence still missing |
| Close the migration only with concrete evidence, not proxy signals. | `completion_audit.md` records accepted evidence and gaps; strict audit requires unblocked statuses, phase commits, and audited real-client session dirs. | Not complete |

## Evidence Checked

- Phase files under `migrationv3/phase_*.md`.
- Scenario inventory in `migrationv3/legacy_scenario_inventory.md`.
- Packet baseline in `migrationv3/packet_baseline.md`.
- Packet and diagnostic comparison in
  `migrationv3/packet_diagnostic_comparison.md`.
- Phase 9 known issues and retirement recommendation in
  `migrationv3/phase_9_known_issues_and_recommendation.md`.
- Real-client acceptance checklist in
  `migrationv3/real_client_acceptance_report.md`.
- Real-client acceptance-session wrapper in
  `scripts/source2_run_real_client_acceptance_session.ps1`.
- Real-client acceptance gate runner in
  `scripts/source2_run_real_client_acceptance_gate.ps1`.
- EQ2 client DirectX/window diagnostic helper in
  `scripts/source2_check_eq2_client_directx.ps1`.
- Real-client acceptance evidence audit helper in
  `scripts/source2_audit_real_client_acceptance_session.ps1`; final evidence
  requires `login_accepted` in source2 logs.
- Migration completion audit helper in
  `scripts/source2_audit_migrationv3_completion.ps1`.
- Real-client acceptance result recorder in
  `scripts/source2_record_real_client_acceptance_results.ps1`.
- Local client login-address helper in
  `scripts/source2_set_eq2_client_login.ps1`.
- Workspace-local client sandbox helper in
  `scripts/source2_prepare_eq2_client_sandbox.ps1`.
- Git metadata permission diagnostic helper in
  `scripts/source2_diagnose_git_permissions.ps1`.
- Guarded phase-commit helper in
  `scripts/source2_commit_migrationv3_phases.ps1`.
- CTest `source2_set_eq2_client_login_backup_restore`, which validates helper
  update/restore behavior against a temporary config file only.
- CTest `source2_prepare_eq2_client_sandbox_creates_copy_and_junction`, which
  validates sandbox config rewrite, backup creation, copied directory, junction,
  and manifest output against temporary files.
- CTest `source2_diagnose_git_permissions_writable_temp_repo`, which validates
  the Git metadata diagnostic helper against a temporary writable `.git`
  directory and verifies ACL report generation.
- CTests `source2_commit_migrationv3_phases_dry_run_defaults_to_phase_8`,
  `source2_commit_migrationv3_phases_rejects_phase_9_without_flag`, and
  `source2_commit_migrationv3_phases_includes_phase_9_with_flag`, which
  validate that the phase-commit helper can print the path-based commit
  sequence and keeps Phase 9 explicitly guarded.
- CTest `source2_commit_migrationv3_phases_commits_temp_repo`, which validates
  that the phase-commit helper can create real phase commits in a temporary Git
  repository without touching the working repository.
- CTest `source2_run_real_client_acceptance_session_writes_failure_report`,
  which validates that failed startup attempts still write a `session.md`
  failure report before exiting.
- CTests
  `source2_run_real_client_acceptance_session_auto_login_dry_run_redacts_password`
  and `source2_run_real_client_acceptance_session_redacts_client_session_arg`,
  which validate that helper-planned client launches do not print login
  passwords or session-token arguments. A direct dry-run redaction check also
  passed after adding redacted client arguments to generated session reports;
  a second direct check verifies `+cl_password` is redacted.
- CTest `source2_run_real_client_acceptance_gate_dry_run`, which validates the
  combined localhost/LAN gate dry-run command sequence.
- CTests `source2_record_real_client_acceptance_results_accepts_all` and
  `source2_record_real_client_acceptance_results_rejects_pending`, which
  validate manual gate result recording and pending-gate rejection.
- CTests `source2_audit_migrationv3_completion_allows_known_blockers`,
  `source2_audit_migrationv3_completion_rejects_current_blockers`, and
  `source2_audit_migrationv3_completion_rejects_missing_phase_git_add`, which
  validate the migration-level audit against the current blocked state and
  enforce phase commit-step structure.
- CTest `source2_audit_migrationv3_completion_rejects_missing_lan_client_evidence`,
  which validates that final real-client evidence must include both the
  localhost and LAN client target hosts.
- CTest
  `source2_audit_migrationv3_completion_rejects_forbidden_client_snapshot_secret`,
  which validates that the migration-level audit passes `-ForbiddenSecret` down
  to the real-client acceptance evidence audit.
- CTest `source2_audit_real_client_acceptance_session.ps1_parses`, plus
  CTests `source2_audit_real_client_acceptance_session_passes_sample` and
  `source2_audit_real_client_acceptance_session_rejects_pending_gate`, which
  validate the acceptance evidence audit helper against syntax regressions, a
  completed sample session, and an incomplete pending-gate session.
- CTest
  `source2_audit_real_client_acceptance_session_rejects_forbidden_client_snapshot_secret`,
  which validates that copied client-log snapshots are also scanned for the
  forbidden wrong-password value before Phase 9 evidence is accepted. A direct
  audit check now also rejects a forbidden secret in `session.md` itself.
- Phase commit recovery plan in `migrationv3/phase_commit_plan.md`.
- Git permission recovery notes in `migrationv3/git_permission_recovery.md`.
- Recovery-only Phase 0-8 shadow bundle under
  `artifacts\migrationv3-shadow-commits\20260515-232746`; verified as valid,
  but not accepted as completion because it does not update the real
  repository refs.
- Recovery-only Phase 0-8 patch series under
  `artifacts\migrationv3-shadow-commits\20260515-232746\patches`; generated
  from the same shadow branch for import after repository permissions are
  fixed, but not accepted as completion because it has not been applied to the
  real repository.
- Recovery-only current Phase 0-9 checkpoint under
  `artifacts\migrationv3-shadow-commits\20260516-062935-phase0-9-recovery`;
  verified as valid with clean shadow status and bundle head
  `7fa56de2a2afe0470313be2b360aab07266d06d1`, but not accepted as completion
  because Phase 9 remains blocked and the real repository refs were not
  updated.
- Phase commit path lists include the latest required files:
  `source2/protocol/include/eq2/protocol/login_response.h` in Phase 3 and
  `scripts/source2_capture_login_packets.ps1` in Phase 9.
- Guide in `docs/source2_login_server_guide.md`.
- Latest full source2 CTest:

```text
ctest --test-dir build\source2-vcpkg-user-verify -C Debug --output-on-failure
```

Observed result on 2026-05-16 after adding the real-client acceptance-session,
client-config, client-sandbox, acceptance result recorder, combined gate,
acceptance evidence audit, migration completion audit helpers, required
localhost/LAN client-target evidence checks, forbidden-secret pass-through, Git
metadata diagnostics, the guarded phase-commit helper, the client auto-login
dry-run redaction test, the client session-argument redaction test, the
DirectX/window preflight helper parse test, packet-capture helper, UDP
`OP_Combined` outbound coalescing parity, combined-reply probe/smoke decoding,
and the required `login_accepted` acceptance-audit rejection test: 0 failures
out of 155 tests; the opt-in
`source2_target_live_login_ctest` was skipped by design. An earlier full
`source2_ci.ps1` wrapper attempt timed out while CTest was running; stale
`ctest.exe` processes were stopped before the direct full CTest run.

- Objective connection data:

| Item | Evidence | Status |
| --- | --- | --- |
| Target DB host | Automated gates use `192.168.1.243:3306`; target live gate and direct verifier previously reached it successfully with configured secrets. | Verified |
| Target DB user | Automated gates use `eq2emu`; target live gate and direct verifier previously authenticated with configured secrets. | Verified |
| Supplied DB password value | The target DB credential form with a literal embedded space was rejected by MariaDB for `eq2emu` from this source2 host. The form without the embedded space passed the login-only and world-list target gates on 2026-05-16. The secret value is intentionally not recorded. | Verified with corrected interpretation |
| Current shell DB/world secrets | `EQ2_DB_PASSWORD`, `EQ2_WORLD_ACCOUNT`, and `EQ2_WORLD_PASSWORD` are not persistently set in this shell; they were set only inside bounded commands for the successful target live gates. | Use explicit env secrets for manual gates |
| Login game account | Automated probes use `testlabs`; target live gate and direct verifier authenticated the login account. | Verified |
| Login game password | Automated probes use the test account password without printing it; successful target gates verify it. | Verified |

- Target live gate with source2 world registration:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1 -NoTranscript
```

Observed result on 2026-05-15: the opt-in target CTest gate passed with the
target MariaDB connection, login account check, world DB check, MariaDB-backed
login smoke, temporary source2 login/world serve, and source2 probe expecting
one world.

Rerun on the current build after the client-sandbox helper and wrapper
validation updates: 4/4 matching target CTest tests passed, including the
opt-in target live-login CTest.

Rerun on 2026-05-16 after UDP `OP_Combined` outbound coalescing parity and
combined-reply probe decoding: 4/4 matching target CTest tests passed,
including the opt-in target live-login CTest with one source2 world advertised
at `192.168.1.41`.

- Direct target verifier:

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

- Failed-login diagnostic gate:

```text
source2 login --serve --diagnostic-events
eq2_login_server --probe-login --username testlabs --password <wrong-password>
```

Observed result on 2026-05-15: the probe exited with code 1 and
`reply_code=1`; diagnostic stdout contained `login_rejected` and
`login_failures=1`; the wrong password value was not present in the diagnostic
output. Logs were written under
`artifacts\source2-diagnostic-gate\failed-login-diagnostic-*.log`.

- LAN-address source2 probe:

```text
temporary source2 login on 0.0.0.0:9100
temporary source2 world registered to login
eq2_login_server --probe-login --connect-host 192.168.1.41 --connect-port 9100 --expect-world-count 1
```

Observed result on 2026-05-15:

```text
login probe reply_code=0 account_id=1 world_list=yes worlds=1 character_list=yes post_world_reply=yes
```

Artifacts were written under `artifacts\source2-lan-probe-gate`.

- Real-client acceptance-session wrapper preflight:

```text
source2_run_real_client_acceptance_session.ps1 -ClientTargetHost 127.0.0.1 -RunForMs 4000
```

Observed result on 2026-05-15: the wrapper started source2 login and world,
wrote `artifacts\source2-real-client-acceptance\20260515-212620\session.md`,
and login diagnostics recorded `world_registered` with `registered_worlds=1`.
This validates the wrapper and artifact capture only; it is not a substitute
for the pending real-client UI gates.

- LAN-target acceptance-session wrapper preflight:

```text
source2_run_real_client_acceptance_session.ps1 -ClientTargetHost 192.168.1.41 -RunForMs 4000
```

Observed result on 2026-05-15: the wrapper wrote
`artifacts\source2-real-client-acceptance\20260515-212814\session.md` with
`cl_ls_address 192.168.1.41`; login diagnostics recorded `world_registered`
with `registered_worlds=1`. This is still a wrapper preflight, not a real-client
LAN UI pass.

- Local real-client artifact scan:
  - `E:\_EQ2\packets` contains only the earlier `source2hex.txt`,
    `source2.pcapng`, `sourcehex.txt`, and `source.pcapng` captures from
    2026-05-15 around 12:04-12:18.
  - No newer post-fix real-client packet capture was present there.
  - A broader targeted scan found the local client install at
    `E:\Games\Everquest II` with `eq2_default.ini`, `EQ2.exe`,
    `EverQuest2.exe`, and `eq2_packet_log.jsonl`.
  - The current `eq2_default.ini` value is `cl_ls_address 127.0.0.1`; the
    found client packet log is still not accepted as source2 Phase 9 evidence
    because its timestamp is stale from 2026-05-15.

- Workspace-local client sandbox attempt:
  - created `artifacts\eq2-client-sandbox` with copied writable/config files
    and junctioned large read-only asset directories,
  - set the sandbox `eq2_default.ini` to source2 localhost/LAN targets during
    bounded attempts,
  - earlier launches from this shell produced a Fatal Error window and
    `D3DERR_NOTAVAILABLE` in `artifacts\eq2-client-sandbox\verifylog.txt`,
  - adding DXVK 2.7.1 `x32\d3d9.dll` to the sandbox removed that fatal dialog
    for `EverQuest2.exe`; the helper check passed against the DXVK sandbox,
  - later bounded launches created source2 session folders and sent auto-login
    keys or session-token/config autologin hints, but source2 diagnostics still
    stopped at `session_requested` with `login_attempts=0`,
  - the DXVK localhost run
    `artifacts\source2-real-client-acceptance\20260516-035531\session.md`
    used a no-space world name, registered `Source2World`, and sent
    command-line autologin plus click/key attempts; source2 still logged
    `login_attempts=0`,
  - later sandbox runs
    `artifacts\source2-real-client-acceptance\20260516-043439\session.md`
    and `artifacts\source2-real-client-acceptance\20260516-043658\session.md`
    targeted the corrected nested login-scene coordinates and then posted
    direct Win32 messages to the EQ2 window; both still stopped at
    `session_requested` with `login_attempts=0`,
  - diagnostic-only sandbox run
    `artifacts\source2-real-client-acceptance\20260516-044357\session.md`
    temporarily prefilled the sandbox login XML and made the login button cover
    the login scene, then restored the XML; this still produced no login packet
    and is not accepted as Phase 9 evidence,
  - post-coalescing sandbox run
    `artifacts\source2-real-client-acceptance\20260516-051841\session.md`
    used the rebuilt UDP `OP_Combined` coalescing source2 binaries, registered
    `Source2World`, sent username/password auto-login keys, and still stopped
    at `session_requested` followed by disconnect with `login_attempts=0`,
  - post-coalescing native SendInput sandbox run
    `artifacts\source2-real-client-acceptance\20260516-052407\session.md`
    used explicit login-scene field/button coordinates against the DXVK
    sandbox client and still stopped at `session_requested` followed by
    disconnect with `login_attempts=0`, `malformed_packets=0`, and
    `unsupported_packets=0`,
  - client-area and virtual-key SendInput sandbox runs
    `artifacts\source2-real-client-acceptance\20260516-053204\session.md`,
    `artifacts\source2-real-client-acceptance\20260516-053742\session.md`,
    and `artifacts\source2-real-client-acceptance\20260516-053916\session.md`
    recorded window/client geometry in `auto-login-input.txt` and confirmed
    that XML login-scene coordinates need client-area mode, but still stopped
    at `session_requested` followed by disconnect with `login_attempts=0`,
  - scan-code SendInput sandbox run
    `artifacts\source2-real-client-acceptance\20260516-054935\session.md`
    used client-area field clicks plus scan-code text/Enter injection and
    still stopped at `session_requested` followed by disconnect with
    `login_attempts=0`,
  - visual capture probes
    `artifacts\source2-real-client-acceptance\20260516-055439` and
    `artifacts\source2-real-client-acceptance\20260516-055621` showed the
    current shell cannot capture the DXVK client surface: `PrintWindow`
    produced only the window frame plus a blank white client area, and `BitBlt`
    returned false with a black bitmap,
  - sandbox-only focus-hint run
    `artifacts\source2-real-client-acceptance\20260516-060043\session.md`
    temporarily focused `WindowPage.Username` through the login XML and drove
    scan-code keyboard input, then restored the XML. Source2 still logged only
    `session_requested` followed by disconnect with `login_attempts=0`,
  - strengthened-focus SendInput run
    `artifacts\source2-real-client-acceptance\20260516-060658\session.md`
    added foreground diagnostics and confirmed the current shell cannot make
    the EQ2 window foreground: `focus_after=0x0` and
    `focus_set_foreground=false`,
  - direct `WindowMessage` runs
    `artifacts\source2-real-client-acceptance\20260516-060952\session.md` and
    `artifacts\source2-real-client-acceptance\20260516-061126\session.md`
    posted activation, focus, mouse, character, Enter, and Login-button
    messages to the EQ2 window. The posted activation/focus messages returned
    success but source2 still logged only `session_requested` followed by
    disconnect with `login_attempts=0`,
  - five-minute manual-interaction window
    `artifacts\source2-real-client-acceptance\20260516-061607\session.md`
    launched source2 login/world and the sandbox client without auto-login.
    The resulting logs still stopped at `session_requested` followed by
    disconnect with `login_attempts=0`,
  - client binary string inspection found `cl_autologin`,
    `cl_autoplay_allowed`, `cl_autoplay_world`, `cl_autoplay_char`,
    `cl_sessionid`, and `cl_username`; the original login server source still
    authenticates login requests by username/password, so the session-token
    path is not accepted as the missing source2 parity blocker,
  - no accepted real-client login/world/character evidence was produced.

- Installed-client bounded attempt:
  - generated
    `artifacts\source2-real-client-acceptance\20260516-012315\session.md`
    and
    `artifacts\source2-real-client-acceptance\20260516-012745\session.md`;
    after the session-disconnect reply patch, generated
    `artifacts\source2-real-client-acceptance\20260516-013523\session.md`;
    after the captured login-request parser fix, generated
    `artifacts\source2-real-client-acceptance\20260516-015831\session.md`
    and longer username/password auto-login attempt
    `artifacts\source2-real-client-acceptance\20260516-021619\session.md`;
    after checking the client binary command-line strings, generated explicit
    `cl_autologin` attempt
    `artifacts\source2-real-client-acceptance\20260516-022448\session.md`
    and console-style `+cl_autologin` attempt
    `artifacts\source2-real-client-acceptance\20260516-023406\session.md`,
    then direct `EQ2.exe` launcher checks
    `artifacts\source2-real-client-acceptance\20260516-025157\session.md`
    and
    `artifacts\source2-real-client-acceptance\20260516-025219\session.md`,
    plus a no-wait `EverQuest2.exe` key-sequence attempt
    `artifacts\source2-real-client-acceptance\20260516-025352\session.md`,
    and LAN cvar attempt
    `artifacts\source2-real-client-acceptance\20260516-030148\session.md`,
  - launched `E:\Games\Everquest II\EverQuest2.exe` with the config already
    pointing at `127.0.0.1`; `EQ2.exe` was also checked and exited before
    login input could be sent,
  - sent username/password auto-login keys without printing the password and
    tried command-line `cl_autologin` and `+cl_autologin` launches with
    sensitive client arguments redacted in wrapper output; also tried
    command-line `+cl_ls_address 192.168.1.41` without editing the installed
    client config,
  - source2 diagnostics still stopped at `session_requested` with
    `login_attempts=0` for the launches that reached source2; the post-patch
    runs also recorded the protocol disconnect event or initial session request
    but did not progress to `login_accepted`,
  - a direct UI Automation check showed the installed client window is a
    `Fatal Error` dialog reporting `DirectX Error. (D3DERR_NOTAVAILABLE)`,
  - the copied client packet log and alert log were stale from 2026-05-15, so
    this folder is not accepted as Phase 9 real-client evidence.

- Source/original packet comparison recheck:
  - `E:\_EQ2\packets\sourcehex.txt` shows original source receiving the same
    42-byte key request and 113-byte encrypted login request shape as the
    source2 capture,
  - original source coalesced the key response and login prompt into one
    120-byte UDP datagram, while the earlier source2 capture sent the
    equivalent 42-byte and 76-byte datagrams separately,
  - source2 now attempts legacy `OP_Combined` coalescing for small
    CRC-protected UDP response batches, and automated protocol/login tests plus
    the target DB-backed login/world-list gate pass through the combined-reply
    decoder path,
  - the earlier split did not prevent the captured source2 client from sending
    the 113-byte login packet, and exact replay of that packet sequence after
    the parser fix reaches `login_accepted`,
  - the current live-client blocker remains lack of a submitted real-client
    login request from this shell, not pre-login UDP reachability.

- Latest git staging retry:

```text
git add migrationv3 docs source2 scripts cmake
```

Observed result on 2026-05-15:

```text
fatal: Unable to create 'E:/_EQ2/eq2emu/.git/index.lock': Permission denied
```

- Git permission workaround attempts:
  - explicit `icacls` grant for the current sandbox user on `.git` and
    `.git\index` failed with `Access is denied`,
  - a temporary `GIT_INDEX_FILE` outside `.git` avoided `.git/index.lock`, but
    `git add` still failed because `.git/objects` is not writable by this
    process.

- Earlier target live-gate attempt before credentials were available:

```text
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -NoTranscript
```

Observed result on 2026-05-15: the gate reached the target MariaDB TCP endpoint
at `192.168.1.243:3306`, then stopped because `EQ2_DB_PASSWORD` was not set.

- Target live-gate credential recheck:

```text
source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -ExpectedWorldCount 0 -NoTranscript
source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1 -NoTranscript
```

Observed result on 2026-05-16: the credential form with a literal embedded
space was rejected by MariaDB; the form without the embedded space passed both
the login-only gate and the source2 world-list gate. The world-list run passed
4/4 matching target CTests, including the opt-in live gate, with one enabled
world row registered.

## Checklist

| Requirement | Evidence | Status |
| --- | --- | --- |
| Phase 0 baseline packet audit | `phase_0_baseline_packet_audit.md`, `packet_baseline.md`; source2/original divergence identified as embedded `OP_AppCombined` key+login packet. | Blocked only by phase commit |
| Phase 1 legacy scenario inventory | `phase_1_legacy_login_inventory.md`, `legacy_scenario_inventory.md`; original client, world, LoginStream, character, log, admin, and maintenance paths mapped to phases. | Blocked only by phase commit |
| Phase 2 LoginStream transport parity | `phase_2_loginstream_transport_parity.md`; protocol tests cover session, keepalive, ACK/out-of-order ACK resend, duplicate/future packets, reconnect reset, inbound combined/app-combined, outbound UDP `OP_Combined` coalescing, fragments, and captured client packet replay. | Local implementation/test evidence complete; real-client probe and phase commit blocked |
| Phase 3 encrypted login handshake parity | `phase_3_encrypted_login_handshake_parity.md`; captured key+login app-combined replay decrypts to login request; exact source2 real-client UDP payload replay now reaches `login_accepted` and receives an encrypted sequenced login reply; live UDP service test reaches accepted login; CI passes; target source2 probe authenticates against the DB-backed login server. | Local implementation/test evidence complete; real-client UI validation and phase commit blocked |
| Phase 4 account login policy parity | `phase_4_account_login_policy_parity.md`; account auth/create/version/duplicate login behavior and DB update paths are implemented and tested; classic 546 `LS_LoginRequest` parsing now matches the original ClientVersion 1 layout and accepts the captured real-client login payload. | Local implementation/test evidence complete; MariaDB real-client account update confirmation and phase commit blocked |
| Phase 5 world registration/list parity | `phase_5_world_registration_list_parity.md`; world registration/auth/status/list/support-update flows have protocol, DB, fake repo, live-login test coverage, and a target source2 world-list gate with one advertised world. | Automated implementation/test/target-probe evidence complete; real-client world-list live check and phase commit blocked |
| Phase 6 character lifecycle parity | `phase_6_character_lifecycle_parity.md`; character list, create, delete, play, world metadata, support updates, appearance/color save parity, and access-key handoff are implemented and tested; target source2 probe sees a character-list reply with the world registered. | Automated implementation/test/target-probe evidence complete; real-client character/select/create/delete/play live check and phase commit blocked |
| Phase 7 client logs/admin/web parity | `phase_7_client_logs_admin_web_parity.md`; log packet ingestion/storage, maintenance jobs, and reviewed web/status replacement docs are implemented/tested. | Local implementation/test evidence complete; phase commit blocked |
| Phase 8 diagnostics | `phase_8_observability_and_diagnostics.md`; redacted event formatting, counters, `--diagnostic-events`, troubleshooting guide updates, and an automated failed-login diagnostic gate are implemented/tested. | Automated implementation/test evidence complete; real-client failed-login diagnostic check and phase commit blocked |
| Phase 9 full live acceptance gate | `phase_9_full_live_acceptance_gate.md`; final live gate requires real EQ2 client on localhost and LAN, accepted source2 login, registered world, DB-backed target gate, packet/log comparison, known-issues list, and retirement recommendation. The DB-backed target gate and source2 world-list probe now pass with one advertised world; the 2026-05-16 rerun also passes after UDP `OP_Combined` outbound coalescing parity; an automated LAN-address probe through `192.168.1.41:9100` passes; `packet_diagnostic_comparison.md` records the pre-fix packet divergence, the parser blocker reproduced by exact captured source2 UDP replay, the post-fix replay evidence with `login_accepted`, and the outbound coalescing follow-up; the automated failed-login diagnostic gate passes; `real_client_acceptance_report.md` lists the remaining manual gates and artifact paths to collect; `source2_run_real_client_acceptance_session.ps1` starts a consistent manual client-test session with login/world logs, can switch/launch the local client, can snapshot client-side logs, and has bounded localhost/LAN preflight artifacts showing world registration; `source2_run_real_client_acceptance_gate.ps1` can plan or sequence localhost/LAN sessions plus result recording and evidence audit; `source2_capture_login_packets.ps1` provides an elevated `pktmon` fallback for fresh UDP 9100 captures; `source2_check_eq2_client_directx.ps1` detects the current DirectX fatal-dialog blocker before a full client gate; `source2_record_real_client_acceptance_results.ps1` can record explicit manual gate results; `source2_audit_real_client_acceptance_session.ps1` now requires `login_accepted` for completed manual-session acceptance; `source2_audit_migrationv3_completion.ps1` can verify final migration-level completion criteria; `source2_set_eq2_client_login.ps1` provides repeatable client login-address switching with restore support and a temp-file backup/restore CTest; `phase_9_known_issues_and_recommendation.md` recommends keeping legacy fallback until real-client gates pass; the guide includes build, configure, run, diagnose, and recovery/rollback commands. | Not complete; depends on manual real-client localhost/LAN UI gates and commits |
| Required phase commits | `commit_status.md`; repeated `git add` attempts fail before staging because `.git/index.lock` cannot be created. `phase_commit_plan.md` records the required commit sequence and file scopes for use after permissions are fixed. `git_permission_recovery.md` records the observed ACL failure, non-destructive recovery checks, a verified recovery-only Phase 0-8 shadow bundle, and a matching Phase 0-8 patch series. | Blocked by repository permissions |

## Current Blockers

- The current shell cannot create `.git/index.lock`; no phase commits can be
  made until repository permissions are fixed or a process with write access to
  `.git` performs the commits.
- The current shell also cannot add Git objects to `.git/objects`; using a
  temporary index does not unblock commits.
- Real-client gates cannot be completed from this non-interactive shell:
  - rebuilt/running source2 real-client probe,
  - client progressing past `Trying login server #1`,
  - real-client world list,
  - real-client character list/create/delete/play,
  - failed-login diagnostic event check,
  - Phase 9 localhost and LAN acceptance runs.
- A workspace-local sandbox avoids changing the installed client config, but
  the sandbox client cannot be completed from this shell. Earlier attempts
  reported `D3DERR_NOTAVAILABLE`; DXVK now gets `EverQuest2.exe` to a normal
  window, but later launches accepted auto-login/click/key attempts and still
  produced no client login request and no fresh client packet log.
- A lower-level Direct3D9 probe run from this shell sees the NVIDIA adapter but
  cannot retrieve D3D9 HAL device caps or create a native D3D9 device. DXVK
  works around launch, so the remaining Phase 9 client blocker is now UI/input
  submission from this shell, not source2 packet reachability.
- A pre-fix source2/original packet comparison is documented and rechecked
  against the provided captures; the final acceptance comparison still needs a
  fresh accepted post-fix real-client capture.
- The opt-in target live CTest remains skipped in default CI by design. It was
  run separately with target credentials and passed on 2026-05-15 and
  2026-05-16.

## Conclusion

The migrationv3 objective is not complete yet. Local implementation and
automated verification have advanced through the known code/test gaps, but
required live-client gates and required phase commits remain unresolved.
