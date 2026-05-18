# Source2 Login Server Guide

This guide is for running the source2 login server from a PowerShell prompt at
the repo root. The current target template uses MariaDB at `192.168.1.243:3306`,
login DB `eq2ls`, DB user `eq2emu`, client version `546`, and DB-backed
opcodes. For a different environment, edit the deploy config and pass matching
script parameters.

## 1. Build

Build the source2 apps with MariaDB Connector/C through vcpkg:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
```

The login server executable is created at:

```text
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe
```

## 2. Configure

Copy the no-secret target templates outside source control:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_prepare_deploy_config.ps1 -OutputDir artifacts\source2-deploy-config
```

Edit `artifacts\source2-deploy-config\login_server.eq2emu-target.ini`.
Keep these login settings unless you are intentionally changing client support:

```ini
[login]
address = 0.0.0.0
port = 9100
client_version = 546
opcode_source = database
opcode_width = packed

[db]
host = 192.168.1.243
port = 3306
name = eq2ls
user = eq2emu
tls = false
```

Do not commit real passwords. Either replace `db.password` only in the deploy
copy, or leave it as `change-me` and set secrets in the shell:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-account-password>"
```

The DB user must be able to connect from the source2 host and read the login
tables checked by source2: `account`, `login_versions`, `login_worldservers`,
`login_bannedips`, `opcodes`, and `ls_world_zones`. The login account used for
testing must exist in `eq2ls.account`; source2 checks `passwd = sha2(password,
512)`.

## 3. Verify

Run the target login gate before serving:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-account-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify
```

Expected result:

```text
source2 target live login gate passed.
```

To prove the source2 world-list path too, use a world account from
`eq2ls.login_worldservers` and expect one registered world:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-account-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1
```

## 4. Open The Login Port

Source2 binds UDP `9100` for login clients and TCP `9100` for world-server
registration. Preview the firewall rules:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_configure_login_firewall.ps1 -LoginPort 9100 -DryRun
```

Run the same command from an elevated PowerShell prompt without `-DryRun` to
create the rules.

## 5. Run

Start the checked wrapper. It validates config, checks MariaDB, checks the login
account/opcodes, and then starts `eq2_login_server --serve`:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-account-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\login_server.eq2emu-target.ini
```

Use `-SkipAccountPreflight` only when you want to start without a test login
password. Leave this PowerShell window open while clients connect.

In another PowerShell window, probe the running server:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-account-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\login_server.eq2emu-target.ini -ConnectHost 127.0.0.1 -ConnectPort 9100 -ExpectedWorldCount 0
```

Use the server's LAN or public IP for `-ConnectHost` when probing from another
machine.

## 6. Point The Client At Source2

On the client machine, edit the EQ2 client's `eq2_default.ini` and add or update
the login server address:

```ini
cl_ls_address <source2-login-host-or-ip>
```

Use `127.0.0.1` only when the client is running on the same machine as the login
server, for example `cl_ls_address 127.0.0.1`. For another PC on the LAN, use
the source2 host's LAN IP, for example `cl_ls_address 192.168.1.41`. For internet
clients, use the public DNS name or public IP that forwards UDP `9100` to the
source2 host. Keep the login server on port `9100`; some clients can parse an
explicit `host:port`, but the legacy client works with the host/IP alone when
the server uses the default port.

Start the client and log in with an account from `eq2ls.account`. If the client
reaches login but shows no worlds, the login server is reachable and no world
server is currently registered.

To advertise a world, configure
`artifacts\source2-deploy-config\world_server.eq2emu-target.ini` with a
client-reachable `advertised_address`, set its world `account` and `password` to
match a row in `eq2ls.login_worldservers`, and set `[login] remote_address` /
`remote_port` to the running login server. Keep `[world] opcode_width = packed`
for the 2006 client's packed `VeType` app-message prefix. Then start the world
wrapper:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_world_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\world_server.eq2emu-target.ini
```

Rerun the login probe with `-ExpectedWorldCount 1` after the world registers.

For the Phase 9 real-client gate, use the acceptance-session wrapper so the
login/world logs, optional client-log snapshots, and session checklist land in
one artifact directory:

Before launching the full gate, verify that the local client can create its
DirectX window from the shell you will use for testing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_check_eq2_client_directx.ps1 -ClientExePath "E:\Games\Everquest II\EverQuest2.exe"
```

If this reports `D3DERR_NOTAVAILABLE`, move the real-client gate to an
interactive desktop or machine where the 2006 client can render.

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 127.0.0.1
```

For the LAN gate, run the same command with `-ClientTargetHost 192.168.1.41`,
set the client to `cl_ls_address 192.168.1.41`, and record the generated
`session.md` path in `migrationv3\real_client_acceptance_report.md`.

To switch the local 2006 client config repeatably, use the helper in dry-run
mode first:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_set_eq2_client_login.ps1 -ClientDir "E:\Games\Everquest II" -LoginHost 192.168.1.41 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_set_eq2_client_login.ps1 -ClientDir "E:\Games\Everquest II" -LoginHost 192.168.1.41
```

To restore the previous config after testing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_set_eq2_client_login.ps1 -ClientDir "E:\Games\Everquest II" -RestoreOriginal
```

To avoid changing the installed client directory, prepare a workspace-local
sandbox that copies writable/config files and junctions large read-only asset
directories:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_prepare_eq2_client_sandbox.ps1 -SourceDir "E:\Games\Everquest II" -OutputDir artifacts\eq2-client-sandbox -LoginHost 192.168.1.41 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_prepare_eq2_client_sandbox.ps1 -SourceDir "E:\Games\Everquest II" -OutputDir artifacts\eq2-client-sandbox -LoginHost 192.168.1.41 -Force
```

The sandbox helper writes `source2_sandbox_manifest.tsv` in the sandbox
directory so the copied files, copied directories, and junctioned asset
directories are auditable.

For a single interactive manual gate, the acceptance-session wrapper can switch
the config, launch the client, snapshot common client logs, and restore the
config when you press Enter in the wrapper window:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 192.168.1.41 -ClientDir "E:\Games\Everquest II" -UpdateClientConfig -LaunchClient -RestoreClientConfigOnExit -SnapshotClientLogs
```

When using the sandbox, point `-ClientDir` at `artifacts\eq2-client-sandbox`
and omit `-UpdateClientConfig` if the sandbox was already prepared for the
target host.

If the client opens with the username already populated, an interactive desktop
can try the password-only auto-login path. Set `EQ2_LOGIN_PASSWORD` in the
shell first; do not pass the password on the command line:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 127.0.0.1 -ClientDir artifacts\eq2-client-sandbox -UpdateClientConfig -LaunchClient -AutoLoginClient -AutoLoginMode PasswordOnly -SnapshotClientLogs
```

For a sandbox username/password attempt, prefer client-area coordinates from
`UI\Default\eq2ui_loginscene.xml` and virtual-key text injection. The wrapper
will write `auto-login-input.txt` next to the generated `session.md`. Set
`EQ2_LOGIN_USERNAME` and `EQ2_LOGIN_PASSWORD` in the shell first; do not pass
the password on the command line:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 127.0.0.1 -ClientDir artifacts\eq2-client-sandbox -UpdateClientConfig -LaunchClient -AutoLoginClient -AutoLoginMode UsernamePassword -AutoLoginInputMethod SendInput -AutoLoginTextMode VirtualKey -AutoLoginCoordinateMode Client -AutoLoginUsernameX 524 -AutoLoginUsernameY 324 -AutoLoginPasswordX 524 -AutoLoginPasswordY 356 -AutoLoginSubmitX 420 -AutoLoginSubmitY 518 -SnapshotClientLogs
```

If the client ignores virtual-key text, retry with
`-AutoLoginTextMode ScanCode` and omit `-AutoLoginSubmitX` /
`-AutoLoginSubmitY` so the wrapper submits with Enter.

If `auto-login-input.txt` records `focus_after=0x0` or
`focus_set_foreground=false`, the shell could not make the EQ2 window
foreground. In that case, either run the wrapper from an interactive desktop
session or retry the diagnostic direct-message path:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_session.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientTargetHost 127.0.0.1 -ClientDir artifacts\eq2-client-sandbox -UpdateClientConfig -LaunchClient -AutoLoginClient -AutoLoginMode UsernamePassword -AutoLoginInputMethod WindowMessage -AutoLoginCoordinateMode Client -AutoLoginUsernameX 524 -AutoLoginUsernameY 324 -AutoLoginPasswordX 524 -AutoLoginPasswordY 356 -AutoLoginSubmitX 420 -AutoLoginSubmitY 518 -SnapshotClientLogs
```

To plan both localhost and LAN gates in one repeatable flow:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_real_client_acceptance_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -ClientDir "E:\Games\Everquest II" -ClientTargetHosts 127.0.0.1,192.168.1.41 -UpdateClientConfig -LaunchClient -RestoreClientConfigOnExit -SnapshotClientLogs -ForbiddenSecret "<wrong-password-used-for-test>" -DryRun
```

After the client run, record the manual gate results in the generated
`session.md`, then audit the evidence folder before marking the gate complete:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_record_real_client_acceptance_results.ps1 -SessionDir artifacts\source2-real-client-acceptance\<timestamp> -Login Passed -WorldList Passed -CharacterList Passed -Play Passed -CreateDelete Passed -FailedLoginDiagnostic Passed -RequireAllRecorded
```

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_audit_real_client_acceptance_session.ps1 -SessionDir artifacts\source2-real-client-acceptance\<timestamp> -RequireManualPass -RequireLoginAccepted -RequireClientLogSnapshot -RequireWorldRegistered -RequireFailedLoginDiagnostic -ForbiddenSecret "<wrong-password-used-for-test>"
```

After both localhost and LAN session folders pass, run the migration completion
audit with both session directories:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_audit_migrationv3_completion.ps1 -RequireNoBlockedStatus -RequirePhaseCommits -RequireRealClientEvidence -SessionDirs artifacts\source2-real-client-acceptance\<localhost-timestamp>,artifacts\source2-real-client-acceptance\<lan-timestamp> -RequiredClientTargetHosts 127.0.0.1,192.168.1.41 -ForbiddenSecret "<wrong-password-used-for-test>"
```

## 7. Operator Status And Diagnostics

The original login server could expose optional `/status` and `/worlds` web
routes. Source2 does not start a login web server. Use these reviewed
replacements:

```powershell
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --check-login-db --config artifacts\source2-deploy-config\login_server.eq2emu-target.ini
```

Use the probe for live status and world-count checks:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-account-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\login_server.eq2emu-target.ini -ConnectHost 127.0.0.1 -ConnectPort 9100 -ExpectedWorldCount 1
```

For live troubleshooting, start with diagnostic events enabled:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-account-password>"
$env:EQ2_LOGIN_DIAGNOSTIC_EVENTS = "true"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\login_server.eq2emu-target.ini
```

If launching the executable directly, use:

```powershell
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --serve --config artifacts\source2-deploy-config\login_server.eq2emu-target.ini --diagnostic-events
```

To collect a fresh UDP trace during an interactive client attempt, run an
elevated PowerShell window:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_capture_login_packets.ps1 -TargetHost 127.0.0.1 -DurationSeconds 120 -OutputDir artifacts\source2-real-client-acceptance\packet-captures
```

For LAN testing, replace `127.0.0.1` with the source2 host address, for
example `192.168.1.41`. The helper writes the ETL, converted pcapng, and
`capture.md`; use Wireshark filter `udp.port == 9100`.

Diagnostic event names are redacted and safe to collect for support. Useful
ones:

| Symptom | Events to check | Likely cause |
| --- | --- | --- |
| Client stays on `Trying login server #1` | no `transport_accepted`, no `session_requested` | client is not reaching UDP `9100`, wrong `cl_ls_address`, firewall/NAT, or server bound to the wrong address |
| Client reaches server but login fails | `session_requested`, then `login_rejected` | bad username/password, unsupported client version, account already active, or account creation disabled |
| Client asks for key then stalls | `key_requested`, then `malformed` or no login event | encrypted login payload/opcode mismatch or corrupted client packet |
| No worlds after login | `login_accepted`, no `world_registered` | no world server connected to source2 TCP `9100`, bad world account/password, bad world protocol version, or rejected world address |
| World appears locked/offline | `world_status_updated` with negative status | world sent locked/offline status or disconnected |
| Character list is empty | `login_accepted`, `world_registered`, no matching character rows | `login_characters` rows are missing, deleted, on an unavailable world, or not visible to the logged-in account |
| Create/delete/play fails | `character_create_rejected`, `character_rejected`, or `play_rejected` | invalid world, invalid character row, unavailable world, or rejected world response |
| Client log packets fail | `client_log_rejected` | client log packet arrived before login or had malformed compression |
| Packet mismatch | `unsupported` or `malformed` with opcode context | opcode DB/config mismatch or unsupported client packet |

For client version 546, source2 parses create-character customization from the
2006 client serializer, not the older XML gap layout: the v4 customization block
writes version byte `4` and has no 26-byte placeholder before `hair_file`.

For client version 546, source2 corrects the legacy opcode table's client-log
name drift: type `215` is `eq2_crash.log`, type `216` is `alertlog.txt`, and
type `217` is `verifylog.txt`. Override `login.client_eq2_crashlog_reply_opcode`
or `EQ2_LOGIN_CLIENT_EQ2_CRASHLOG_REPLY_OPCODE` only if the deployed opcode data
already carries a corrected name.

At shutdown, diagnostic mode prints counters for total events, active client
sessions, registered worlds, login attempts, failures, malformed packets, and
unsupported packets.

## 8. Recover Or Roll Back

If source2 login fails during live testing, keep the legacy login server as the
fallback until Phase 9 is accepted.

To stop source2, close the PowerShell window running
`source2_start_login_server.ps1`, or press `Ctrl+C` in that window. If a
source2 world wrapper is running, stop that window too.

To return a client to the original login server, edit `eq2_default.ini` and set
`cl_ls_address` back to the original login host, for example:

```ini
cl_ls_address 192.168.1.243
```

To verify the legacy path is restored, start the original login server and log
in with the same client and account. The client should progress past
`Trying login server #1` and show the original world list.

Source2 does not migrate or delete login data during normal startup, probing,
or serving. It writes the same operational rows as the original login server
for accepted logins, world status, character lifecycle, login equipment,
character pictures, and client logs. If a test create/delete/play operation
was performed, verify or clean up the affected `login_characters`,
`login_char_colors`, `login_equipment`, and world character rows before
retesting against legacy.
