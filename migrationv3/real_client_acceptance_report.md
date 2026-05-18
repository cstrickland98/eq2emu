# Real Client Acceptance Report

Status: pending.

## Purpose

Record the post-fix EQ2 real-client acceptance evidence required to close
Phase 9. Automated source2 probes now pass on loopback and through the LAN
address, but the real client remains the acceptance authority for UI/login
behavior.

## Environment

- Source2 host: `192.168.1.41`
- Target DB host: `192.168.1.243:3306`
- Login DB: `eq2ls`
- World DB: `eq2emu`
- Login account: `testlabs`
- Client version: `546`
- Source2 login port: `9100`
- Source2 world port: `9101`
- Original login fallback host: `192.168.1.243`
- Local client install found by scan: `E:\Games\Everquest II`
- Current local client config: `E:\Games\Everquest II\eq2_default.ini`
  contains `cl_ls_address 127.0.0.1` as of the 2026-05-16 installed-client
  attempt

Do not record passwords in this file.

## Preflight Evidence Already Collected

| Gate | Evidence | Status |
| --- | --- | --- |
| Full source2 CTest | 155/155 tests passed on 2026-05-16 after adding UDP `OP_Combined` outbound coalescing parity, combined-reply probe/smoke decoding, acceptance evidence audit, result recorder, combined gate dry-run, wrapper failure-report, client snapshot secret-leakage rejection, required localhost/LAN evidence rejection, migration-level forbidden-secret pass-through, final `login_accepted` audit enforcement, Git metadata diagnostic helper, guarded phase-commit helper, client auto-login dry-run redaction coverage, client session-argument redaction coverage, DirectX/window preflight helper, packet-capture helper, and migration completion audit tests; opt-in target CTest skipped by default. | Passed |
| Target DB-backed source2 gate | `source2_run_target_live_login_gate.ps1 -RunWorldServe -ExpectedWorldCount 1 -WorldAdvertisedAddress 192.168.1.41 -NoTranscript` passed on 2026-05-16 after the UDP coalescing change; 4/4 matching target CTest tests passed including the opt-in live gate. | Passed |
| Direct source2 verifier | `source2_live_login_verify.ps1 -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1` reported `worlds=1`, `character_list=yes`. | Passed |
| LAN-address source2 probe | Probe through `192.168.1.41:9100` reported `worlds=1`, `character_list=yes`. | Passed |
| Failed-login diagnostics | Wrong-password probe emitted `login_rejected`, `login_failures=1`, and no password value. | Passed |
| Acceptance-session wrapper | Bounded localhost run generated `artifacts\source2-real-client-acceptance\20260515-212620\session.md`; login diagnostics recorded `world_registered` and `registered_worlds=1`. | Passed preflight |
| Acceptance-session LAN wrapper | Bounded LAN-target run generated `artifacts\source2-real-client-acceptance\20260515-212814\session.md` with `cl_ls_address 192.168.1.41`; login diagnostics recorded `world_registered` and `registered_worlds=1`. | Passed preflight |
| Acceptance-session failure report | `source2_run_real_client_acceptance_session.ps1` writes a `session.md` with `Status: failed before ready` if startup fails before the ready report is written. | Passed CTest |
| Local client scan | Found `E:\Games\Everquest II\eq2_default.ini`, `EQ2.exe`, `EverQuest2.exe`, and `eq2_packet_log.jsonl`; the config currently points to source2 localhost, but the client-side packet log remains stale from 2026-05-15. | Informational |
| Client config helper dry run | `source2_set_eq2_client_login.ps1` can switch the installed client login address and create a backup; earlier dry-run coverage verified the original-host to source2-host path without changing the real client install. | Passed preflight |
| Client config helper backup/restore test | CTest `source2_set_eq2_client_login_backup_restore` updates and restores a temporary `eq2_default.ini` without touching the real client install. | Passed |
| One-command client gate dry run | `source2_run_real_client_acceptance_session.ps1 -ClientTargetHost 192.168.1.41 -ClientDir "E:\Games\Everquest II" -UpdateClientConfig -LaunchClient -RestoreClientConfigOnExit -SnapshotClientLogs -DryRun` planned config update, config restore, client-log snapshots, and `EverQuest2.exe` launch. | Passed preflight |
| Localhost/LAN gate dry run | `source2_run_real_client_acceptance_gate.ps1` plans localhost and LAN sessions, result recording, and evidence audit commands in one sequence. | Passed dry-run CTest |
| Client sandbox helper | `source2_prepare_eq2_client_sandbox.ps1` prepares a workspace-local client copy with writable/config files copied, large read-only asset directories junctioned, sandbox `cl_ls_address` set to source2, and `source2_sandbox_manifest.tsv` written for audit. | Passed CTest dry-run and temp create/junction/manifest test |
| Acceptance result recorder | `source2_record_real_client_acceptance_results.ps1` updates the generated `session.md` manual gate table and can reject sessions that still have pending results. | Passed accept-all and pending-gate rejection CTests |
| Acceptance evidence audit helper | `source2_audit_real_client_acceptance_session.ps1` validates a completed `session.md`, required source2 logs, required `login_accepted`, world registration diagnostics, client-log snapshot manifest, failed-login diagnostics, client snapshot secret redaction, and manual pass/fail rows. | Passed sample-pass, missing-login-accepted rejection, pending-gate rejection, and client snapshot secret-leakage rejection CTests |
| Migration completion audit helper | `source2_audit_migrationv3_completion.ps1` verifies required migration artifacts, helper scripts, phase commit messages, phase commit-step structure, unblocked statuses, final real-client session evidence for both required client target hosts, and forbidden wrong-password leakage in accepted session evidence. | Passed known-blocker, current-blocker rejection, missing-git-add structural rejection, missing-LAN-evidence rejection, and forbidden-client-snapshot-secret rejection CTests |
| Captured source2 packet replay | `artifacts\source2-captured-client-replay\20260516-015438` first reproduced the current blocker as `malformed login request payload`; after correcting the classic `LS_LoginRequest` layout, `artifacts\source2-captured-client-replay\20260516-015711` replayed the exact source2 real-client UDP payloads and logged `login_accepted`, `login_attempts=1`, `malformed_packets=0`, and an encrypted sequenced login reply. | Passed packet replay; not a substitute for UI acceptance |
| Workspace client sandbox attempt | Created `artifacts\eq2-client-sandbox` to avoid changing the installed client config. Earlier launch attempts from this shell wrote `D3DERR_NOTAVAILABLE` to `verifylog.txt`; adding the official DXVK 32-bit `d3d9.dll` to the sandbox let `EverQuest2.exe` open without a DirectX fatal dialog. Bounded DXVK sessions, including `artifacts\source2-real-client-acceptance\20260516-033829\session.md`, `20260516-035531\session.md`, `20260516-043439\session.md`, `20260516-043658\session.md`, diagnostic-only `20260516-044357\session.md`, post-coalescing `20260516-051841\session.md`, post-coalescing SendInput `20260516-052407\session.md`, client-area SendInput `20260516-053204\session.md`, virtual-key SendInput `20260516-053742\session.md`, delayed virtual-key SendInput `20260516-053916\session.md`, scan-code SendInput `20260516-054935\session.md`, visual capture probes `20260516-055439` and `20260516-055621`, sandbox focus-hint keyboard run `20260516-060043\session.md`, strengthened-focus SendInput run `20260516-060658\session.md`, direct `WindowMessage` runs `20260516-060952\session.md` and `20260516-061126\session.md`, and a five-minute manual-interaction window `20260516-061607\session.md`, registered the source2 world and reached the initial client session request. The later runs tested the corrected nested login-scene coordinates from `LSUsernamePassword.WindowPage`, direct Win32 `PostMessage` input, native Win32 `SendInput` with explicit window-relative and client-relative coordinates, Unicode, virtual-key, and scan-code text modes, a restored sandbox-only login XML prefill/oversized-button diagnostic, a temporary sandbox-only `OnShow` focus hint, stronger `AttachThreadInput` foreground handling, direct posted activation/focus/mouse/key messages, and the rebuilt UDP `OP_Combined` coalescing source2 binaries; source2 still logged `session_requested` followed by disconnect with `login_attempts=0`. The latest diagnostics show `GetForegroundWindow` remains `0x0` and `SetForegroundWindow` returns false from this shell. `PrintWindow` captured only the EQ2 frame plus a blank white Direct3D client area, and `BitBlt` returned false with a black bitmap from this shell. | Not accepted; blocked by local UI/input automation/visibility, not UDP reachability |
| Installed-client bounded attempt | `artifacts\source2-real-client-acceptance\20260516-012315`, `20260516-012745`, `20260516-013523`, post-parser-fix `20260516-015831`, longer username/password auto-login attempt `20260516-021619`, command-line `cl_autologin` attempt `20260516-022448`, console-style `+cl_autologin` attempt `20260516-023406`, direct `EQ2.exe` launcher attempts `20260516-025157` and `20260516-025219`, no-wait `EverQuest2.exe` key-sequence attempt `20260516-025352`, and LAN cvar attempt `20260516-030148` used the installed client. `EQ2.exe` exited before login input could be sent; the `EverQuest2.exe` attempts still did not produce `login_accepted` or a real login request from this shell. A direct UI Automation check of the installed client showed a `Fatal Error` dialog with `DirectX Error. (D3DERR_NOTAVAILABLE)`. | Not accepted; blocked by local DirectX/UI environment |
| Client launch helper | `source2_run_real_client_acceptance_session.ps1` now handles a launch with no client arguments, has an optional `-AutoLoginClient` path that can send username/password or password-only keys without printing the password, supports SendKeys, native Win32 `SendInput`, or direct `WindowMessage` input with optional field/button coordinates, supports screen/window/client coordinate modes plus Unicode, virtual-key, or scan-code text injection for SendInput, writes `auto-login-input.txt` window/client-rect and foreground diagnostics for native-input runs, redacts sensitive client launch arguments such as `cl_sessionid`/`+cl_password` in dry-run/launch output, and records redacted client arguments in new session reports. Parse, dry-run, auto-login redaction, client-argument redaction, and focused wrapper CTests pass. | Helper ready; UI gate still blocked here |
| Client DirectX diagnostic helper | `source2_check_eq2_client_directx.ps1` starts the configured client, captures top-level window text through UI Automation, and fails if the client exits or opens a DirectX fatal-error dialog. | Captured `D3DERR_NOTAVAILABLE` without DXVK; passed against the sandbox `EverQuest2.exe` after adding DXVK |
| Direct3D9 device probe | `artifacts\migrationv3-dxdiag-20260516-032059.txt` reports Direct3D enabled for the NVIDIA adapter, but `artifacts\migrationv3_d3d9_probe.exe` cannot get D3D9 HAL device caps or create a native D3D9 device from this shell. DXVK works around the client launch path. | Native D3D9 remains unavailable here; DXVK removes the launch blocker but not the automated UI submission blocker |
| Client autologin/session-token investigation | The client binary exposes `cl_autologin`, `cl_autoplay_allowed`, `cl_autoplay_world`, `cl_autoplay_char`, `cl_sessionid`, and `cl_username` strings. The original login server source still authenticates `LS_LoginRequest` by username/password and does not treat the access/session token as the credential gate. Source2 session-token, config-based autologin, explicit `cl_autologin`, and console-style `+cl_autologin` command-line attempts from this shell reached `session_requested` but did not produce a real login request. | Informational; not Phase 9 evidence |
| Source/source2 packet comparison | The provided `E:\_EQ2\packets` captures show both original source and source2 receive the same 42-byte key request and 113-byte encrypted login request shape once the client submits credentials. Original source coalesces the key response and login prompt into one 120-byte UDP datagram; source2 now has a bounded UDP `OP_Combined` coalescing path for small CRC-protected response batches, and the target DB-backed login/world-list gate passes with the combined-reply decoder path. A fresh accepted real-client source2 capture is still required. | Automated transport parity improved; final UI capture pending |

## Real Client Gates To Run

Run these with the current source2 build and record the exact date/time,
client config, and pass/fail result. Prefer starting the session with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 127.0.0.1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 192.168.1.41
```

Each run writes a timestamped `session.md` plus login/world stdout and stderr
logs under `artifacts\source2-real-client-acceptance`.

Use `scripts\source2_set_eq2_client_login.ps1` to update
`E:\Games\Everquest II\eq2_default.ini` before each run and to restore the
previous setting after testing.

For the local interactive client run, the acceptance-session wrapper can do the
config switch and launch step in one command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 192.168.1.41 -ClientDir "E:\Games\Everquest II" -UpdateClientConfig -LaunchClient -RestoreClientConfigOnExit -SnapshotClientLogs
```

For sandboxed testing from an interactive desktop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_prepare_eq2_client_sandbox.ps1 -SourceDir "E:\Games\Everquest II" -OutputDir artifacts\eq2-client-sandbox -LoginHost 192.168.1.41 -Force
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 192.168.1.41 -ClientDir artifacts\eq2-client-sandbox -LaunchClient -SnapshotClientLogs
```

If the client opens with the username already populated, an interactive desktop
can also try the password-only auto-login path. Set `EQ2_LOGIN_PASSWORD` in the
shell first; do not put the password in the command line:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 127.0.0.1 -ClientDir artifacts\eq2-client-sandbox -UpdateClientConfig -LaunchClient -AutoLoginClient -AutoLoginMode PasswordOnly -SnapshotClientLogs
```

To run or preview localhost and LAN gates as one operator sequence:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientDir "E:\Games\Everquest II" -ClientTargetHosts 127.0.0.1,192.168.1.41 -UpdateClientConfig -LaunchClient -RestoreClientConfigOnExit -SnapshotClientLogs -ForbiddenSecret "<wrong-password-used-for-test>" -DryRun
```

For a fresh UDP packet trace during an interactive client attempt, run an
elevated PowerShell capture in parallel with the acceptance session:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_capture_login_packets.ps1 -TargetHost 127.0.0.1 -DurationSeconds 120 -OutputDir artifacts\source2-real-client-acceptance\packet-captures
```

For the LAN path, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_capture_login_packets.ps1 -TargetHost 192.168.1.41 -DurationSeconds 120 -OutputDir artifacts\source2-real-client-acceptance\packet-captures
```

After updating the generated `session.md` manual results, run the evidence
audit helper against that session directory:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_record_real_client_acceptance_results.ps1 -SessionDir artifacts\source2-real-client-acceptance\<timestamp> -Login Passed -WorldList Passed -CharacterList Passed -Play Passed -CreateDelete Passed -FailedLoginDiagnostic Passed -RequireAllRecorded
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_audit_real_client_acceptance_session.ps1 -SessionDir artifacts\source2-real-client-acceptance\<timestamp> -RequireManualPass -RequireLoginAccepted -RequireClientLogSnapshot -RequireWorldRegistered -RequireFailedLoginDiagnostic -ForbiddenSecret "<wrong-password-used-for-test>"
```

After the localhost and LAN session folders pass individually, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_audit_migrationv3_completion.ps1 -RequireNoBlockedStatus -RequirePhaseCommits -RequireRealClientEvidence -SessionDirs artifacts\source2-real-client-acceptance\<localhost-timestamp>,artifacts\source2-real-client-acceptance\<lan-timestamp> -RequiredClientTargetHosts 127.0.0.1,192.168.1.41 -ForbiddenSecret "<wrong-password-used-for-test>"
```

| Gate | Required Evidence | Result |
| --- | --- | --- |
| Localhost login | Client on source2 host uses `cl_ls_address 127.0.0.1` and progresses past `Trying login server #1`. | Pending |
| Localhost world list | Same client shows the source2 registered world. | Pending |
| Localhost character list | Same client shows the expected `testlabs` character list. | Pending |
| Localhost play | Same client can select/play a test character far enough to receive the world handoff response. | Pending |
| Localhost failed login diagnostics | Same client attempts a wrong password while source2 runs with `--diagnostic-events`; source2 logs `login_rejected` without logging the password. | Pending |
| LAN login | Client uses `cl_ls_address 192.168.1.41` and progresses past `Trying login server #1`. | Pending |
| LAN world list | LAN client shows the source2 registered world. | Pending |
| LAN character list | LAN client shows the expected `testlabs` character list. | Pending |
| LAN play | LAN client can select/play a test character far enough to receive the world handoff response. | Pending |
| Character create/delete | On a test world/account, create and delete a test character; source2 responses and DB effects match expected legacy behavior. | Pending |

## Capture / Diagnostic Artifacts

Record fresh post-fix artifacts here. Use paths, not pasted packet contents.

| Artifact | Path | Notes |
| --- | --- | --- |
| Source2 localhost capture | Pending | Required if any localhost gate fails or differs from original. Use `scripts\source2_capture_login_packets.ps1` from elevated PowerShell if Wireshark/tshark is not being used. |
| Source2 LAN capture | Pending | Required for Phase 9 final packet comparison. Use `scripts\source2_capture_login_packets.ps1 -TargetHost 192.168.1.41` from elevated PowerShell if Wireshark/tshark is not being used. |
| Original source comparison capture | `E:\_EQ2\packets\sourcehex.txt` / `source.pcapng` | Existing original successful capture. Refresh if target setup changes. A 2026-05-15 scan found no newer post-fix real-client capture in `E:\_EQ2\packets`. |
| Source2 diagnostic log | Pending | Required for failed login and any rejected create/delete/play path. |
| Source2 login stdout/stderr | Pending | Attach when running wrappers manually. |
| Source2 world stdout/stderr | Pending | Attach when running wrappers manually. |
| Acceptance session report | `artifacts\source2-real-client-acceptance\20260515-212620\session.md`; `artifacts\source2-real-client-acceptance\20260515-212814\session.md` | Bounded localhost/LAN preflight only; replace or add fresh paths after the real client is exercised. |
| Incomplete wrapper attempts | `artifacts\source2-real-client-acceptance\20260515-222051`; `artifacts\source2-real-client-acceptance\20260515-222434`; `artifacts\source2-real-client-acceptance\20260516-005223`; `artifacts\source2-real-client-acceptance\20260516-005331`; `artifacts\source2-real-client-acceptance\20260516-010707`; `artifacts\source2-real-client-acceptance\20260516-010813`; `artifacts\source2-real-client-acceptance\20260516-010915`; `artifacts\source2-real-client-acceptance\20260516-012315`; `artifacts\source2-real-client-acceptance\20260516-012745`; `artifacts\source2-real-client-acceptance\20260516-013523`; `artifacts\source2-real-client-acceptance\20260516-015812`; `artifacts\source2-real-client-acceptance\20260516-015831`; `artifacts\source2-real-client-acceptance\20260516-021619`; `artifacts\source2-real-client-acceptance\20260516-022448`; `artifacts\source2-real-client-acceptance\20260516-023406`; `artifacts\source2-real-client-acceptance\20260516-043439`; `artifacts\source2-real-client-acceptance\20260516-043658`; `artifacts\source2-real-client-acceptance\20260516-044357`; `artifacts\source2-real-client-acceptance\20260516-051841`; `artifacts\source2-real-client-acceptance\20260516-052407`; `artifacts\source2-real-client-acceptance\20260516-053204`; `artifacts\source2-real-client-acceptance\20260516-053444`; `artifacts\source2-real-client-acceptance\20260516-053742`; `artifacts\source2-real-client-acceptance\20260516-053916`; `artifacts\source2-real-client-acceptance\20260516-054935`; `artifacts\source2-real-client-acceptance\20260516-055439`; `artifacts\source2-real-client-acceptance\20260516-055621`; `artifacts\source2-real-client-acceptance\20260516-060043`; `artifacts\source2-real-client-acceptance\20260516-060658`; `artifacts\source2-real-client-acceptance\20260516-060952`; `artifacts\source2-real-client-acceptance\20260516-061126`; `artifacts\source2-real-client-acceptance\20260516-061607` | These are not accepted as Phase 9 evidence. The 2026-05-16 auto-login, client-argument, config-based, sandbox, corrected-coordinate, direct Win32 message, native SendInput, client-area coordinate, virtual-key, delayed virtual-key, scan-code, diagnostic UI-prefill, post-coalescing, focus-hint, strengthened-focus, direct `WindowMessage`, manual-window, and installed-client attempts produced source2 `session_requested` and `world_registered` diagnostics, but `login_attempts=0`; the client-side packet log snapshot was stale from 2026-05-15. The latest auto-login diagnostics show the shell cannot make the client foreground (`focus_after=0x0`, `focus_set_foreground=false`), and posted activation/focus/mouse/key messages also do not trigger credential submission. The `20260516-015812` run failed before ready because the installed client directory denied backup creation; the `20260516-053444` no-wait screenshot diagnostic could not use `CopyFromScreen`, `20260516-055439` `PrintWindow` captured a blank white Direct3D client area, and `20260516-055621` `BitBlt` returned false with a black bitmap from this shell. |
| Existing client JSONL log | `E:\Games\Everquest II\eq2_packet_log.jsonl` | Existing client instrumentation log; current client config points at original host, so this is not source2 Phase 9 acceptance evidence. Use `-SnapshotClientLogs` on the acceptance wrapper to collect a post-fix source2 copy. |
| Workspace sandbox verify log | `artifacts\eq2-client-sandbox\verifylog.txt` | Records `D3DERR_NOTAVAILABLE` from the shell-launched sandbox client; this explains why the current non-interactive shell cannot complete real-client UI acceptance. |
| Direct3D9 probe output | `artifacts\migrationv3_d3d9_probe.exe` run from this shell | `GetDeviceCaps` returned `D3DERR_NOTAVAILABLE`; all tested device creation variants failed, so the real EQ2 client cannot be accepted from this shell. |

## Acceptance Decision

Phase 9 remains blocked until every required real-client gate is passed or the
difference is documented and approved.

Final decision:

- [ ] Accept source2 login parity and retire original login for the covered
      deployment.
- [ ] Keep original login as fallback because real-client gates are incomplete.
- [ ] Keep original login as fallback because one or more differences require a
      follow-up migration.

Decision notes:

```text
Pending.
```
