# Source2 Live Login Blocker Resolution

This guide records the blocker investigation for proving the source2 live login
path against the configured MariaDB host.

The current completion decision and prompt-to-artifact checklist are tracked in
`docs\source2_live_login_completion_audit.md`.

## Resolution

Resolved on 2026-05-15. After MariaDB was reachable on `0.0.0.0`, the remaining
source2 fixes were to make MariaDB TLS explicit/default-off for this target and
to resolve relative live-gate build paths to absolute paths before invoking
CTest. The login-only target verifier and the one-command target CTest gate now
pass against `192.168.1.243:3306`.

```text
source2 live login verification passed.
source2 target live login gate passed.
```

The older TCP blocker evidence below is retained as troubleshooting history.

## Historical TCP Blocker

The source2 vcpkg build succeeded and linked MariaDB Connector/C, but the
earlier failure happened before login authentication:

```powershell
Test-NetConnection -ComputerName 192.168.1.243 -Port 3306
```

Expected before source2 can verify the live DB path:

```text
TcpTestSucceeded : True
```

Earlier result from this host:

```text
TcpTestSucceeded : False
```

The lower-level TCP probe reported a local socket permission failure:

```text
An attempt was made to access a socket in a way forbidden by its access permissions 192.168.1.243:3306
```

Until TCP 3306 is reachable, source2 cannot verify the `eq2ls` login DB, the
`eq2emu` world DB, the `testlabs` login account, opcode rows, or a DB-backed
serve/probe run.

## Step 1: Confirm The Source2 Host Address

On the machine running source2, identify the LAN IP that the MariaDB host must
allow:

```powershell
ipconfig
```

or rerun the source2 verifier:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1
```

When TCP fails, the verifier prints local IPv4 candidates to use for firewall
and MariaDB grants.

## Step 2: Rule Out Source2 Host Outbound Blocking

If the verifier prints `[LocalPolicy]` or mentions socket access permissions,
the TCP attempt is being blocked before source2 can prove anything about the
MariaDB service. From a normal PowerShell prompt on the source2 host, retry:

```powershell
Test-NetConnection -ComputerName 192.168.1.243 -Port 3306
```

Source2 also includes a no-secret helper that prints the same TCP gate, a
lower-level socket failure classification, and local IPv4 candidates for
MariaDB firewall/grants:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_check_mariadb_tcp.ps1
```

The helper, live verifier, and checked start wrappers also report the known
`codex_sandbox_offline_block_outbound` firewall rule when it is present, which
means a target-DB gate may need to run from a normal PowerShell prompt or a
network-enabled Codex session.

If that also fails with an access/permission message, check local outbound
firewall or endpoint security policy. A restrictive Windows firewall can be
opened for this target with an elevated PowerShell command:

```powershell
New-NetFirewallRule -DisplayName "MariaDB outbound from source2" -Direction Outbound -Action Allow -Protocol TCP -RemoteAddress 192.168.1.243 -RemotePort 3306
```

If normal PowerShell succeeds but an IDE/sandboxed shell fails, run the final
live verifier from the normal shell or relax the sandbox/network policy for the
verification run.

Earlier local diagnostics from the restricted Codex shell:

- `netsh advfirewall show allprofiles` reports Windows firewall profiles are
  enabled with `AllowOutbound`.
- `netsh advfirewall firewall show rule
  name=codex_sandbox_offline_block_outbound verbose` reports an enabled
  `Codex Sandbox Offline - Block Non-Loopback Outbound` rule with remote IPv4
  ranges that include `192.168.1.243`, so this Codex shell is expected to fail
  the DB TCP gate.
- `Get-NetFirewallProfile` fails with `Access denied`, so rule-level firewall
  inspection cannot be completed from this shell.
- `ipconfig` shows the source2 LAN address is `192.168.1.41/24` with gateway
  `192.168.1.1`.
- `route print -4` sends `192.168.1.0/24` traffic on-link through
  `192.168.1.41`, and `tracert -d -h 4 192.168.1.243` reaches the target in
  one hop.
- The verifier reported `[LocalPolicy]` for `192.168.1.243:3306` before the
  target DB became reachable from this session.

## Step 3: Confirm MariaDB Is Listening On The LAN

On `192.168.1.243`, check that MariaDB is listening on TCP 3306:

```powershell
netstat -ano | findstr :3306
```

or, on Linux:

```bash
ss -ltnp | grep 3306
```

If MariaDB is bound only to `127.0.0.1`, update the MariaDB server config so
`bind-address` is either the LAN address `192.168.1.243` or `0.0.0.0`.

Common config locations:

- Windows service installs: the MariaDB `my.ini` used by the service
- Linux Debian/Ubuntu: `/etc/mysql/mariadb.conf.d/50-server.cnf`
- Linux RHEL-style installs: `/etc/my.cnf` or `/etc/my.cnf.d/server.cnf`

After changing `bind-address`, restart MariaDB.

## Step 4: Open The Host Firewall

On the MariaDB host, allow inbound TCP 3306 from the source2 host only.

Windows example, replace `<source2-ip>` with the source2 machine IP:

```powershell
New-NetFirewallRule -DisplayName "MariaDB from source2" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 3306 -RemoteAddress <source2-ip>
```

Linux firewalld example:

```bash
sudo firewall-cmd --permanent --add-rich-rule='rule family="ipv4" source address="<source2-ip>" port protocol="tcp" port="3306" accept'
sudo firewall-cmd --reload
```

Linux ufw example:

```bash
sudo ufw allow from <source2-ip> to any port 3306 proto tcp
```

Then retry from the source2 host:

```powershell
Test-NetConnection -ComputerName 192.168.1.243 -Port 3306
```

Do not continue to source2 debugging until `TcpTestSucceeded` is `True`.

## Step 5: Confirm MariaDB Grants

After the port is reachable, confirm the `eq2emu` user can connect from the
source2 host and can read both databases.

An optional MariaDB-side helper is available at
`scripts\source2_mariadb_target_preflight.sql.example`. Copy it outside source
control, replace the placeholder passwords, and run it on `192.168.1.243` to
list the bind setting, matching DB users, suggested grant SQL, required schema
objects, account authentication result, opcode rows, and enabled world-server
accounts.

On the MariaDB host:

```sql
select user, host from mysql.user where user = 'eq2emu';
show grants for 'eq2emu'@'%';
```

If there is no grant for `%` or the source2 host IP, create the narrowest grant
that matches your environment. Replace `<source2-ip>` with the source2 host IP
and `<db-password>` with the MariaDB password out of band:

```sql
create user if not exists 'eq2emu'@'<source2-ip>' identified by '<db-password>';
grant select, insert, update, delete on eq2ls.* to 'eq2emu'@'<source2-ip>';
grant select, insert, update, delete on eq2emu.* to 'eq2emu'@'<source2-ip>';
flush privileges;
```

If the user already exists with a different host pattern, adjust the existing
grant instead of duplicating users unnecessarily.

## Step 6: Verify Login DB And Account

Start from `source2\config\login_server.eq2emu-target.ini.example` when creating
the deploy-time login config. Replace `password = change-me` outside source
control before serving. To create editable deploy config copies without storing
secrets in source templates:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_prepare_deploy_config.ps1 -OutputDir artifacts\source2-deploy-config
```

From the source2 host, run:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_USERNAME = "testlabs"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --check-login-db --db-host 192.168.1.243 --db-port 3306 --db-name eq2ls --db-user eq2emu --client-version 546
```

This verifies:

- MariaDB Connector/C can connect.
- The login schema has the required tables and columns.
- The required login opcode rows exist for client version `546`.
- The opcode version range accepts client version `546`.
- The `testlabs` account authenticates with the configured login password.

## Step 7: Verify World DB

Start from `source2\config\world_server.eq2emu-target.ini.example` when creating
the deploy-time world config. Replace DB password and `login_worldservers`
account values outside source control before serving.

From the source2 host:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_world_server.exe --check-world-db --db-host 192.168.1.243 --db-port 3306 --db-name eq2emu --db-user eq2emu
```

This verifies the world DB connection and the minimum zone-bootstrap schema
source2 uses before starting the owner-facing world server path.

## Step 8: Run The Source2 Live Login Gate

After TCP and grants are fixed, prefer the one-command gate. It runs the
no-secret TCP check first, requires secrets only after TCP succeeds, exports the
target settings to the opt-in CTest subprocess, and writes a timestamped
transcript under `artifacts\source2-target-live-gate`:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify
```

The gate prints `source2 target live login gate passed.` only after the CTest
target returns success. This is the login-only gate and is enough to prove that
a user can authenticate against the source2 login server.

Equivalent lower-level verifier command:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -ExpectedWorldCount 0
```

Equivalent manual opt-in CTest gate:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
$env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN = "1"
ctest --test-dir build\source2-vcpkg-user-verify -C Debug -R source2_target_live_login_ctest --output-on-failure
```

The one-command wrapper exports its non-secret parameters to `EQ2_SOURCE2_TARGET_*`
environment variables for the CTest subprocess, so custom host, DB, port, and
world-list values are preserved when `source2_target_live_login_ctest` runs.

Use `-ExpectedWorldCount 0` only when no source2 world server is being started.
To prove that login advertises a registered world, use the world-server account
from `login_worldservers`:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe
```

Equivalent lower-level verifier command:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_live_login_verify.ps1 -BuildDir build\source2-vcpkg-user-verify -RunMariaDbSmoke -RunServeProbe -RunWorldServe -ExpectedWorldCount 1
```

The `-RunWorldServe` command is the stronger readiness gate. It proves source2
login can start, source2 world can register with it, and a login probe receives
the expected non-empty world list.

The older owner preflight wrapper remains available if you want a direct
verifier transcript instead of the CTest gate transcript:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_LOGIN_PASSWORD = "<login-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_owner_live_login_preflight.ps1
```

Use `-RunWorldServe` after setting `EQ2_WORLD_ACCOUNT` and
`EQ2_WORLD_PASSWORD` to capture the stronger non-empty world-list gate.

Before starting long-running owner processes, run the checked wrappers in
`-DryRun` mode to validate config, list required environment secret names unless
the config already provides non-placeholder values, and print the planned
preflight/start or probe command sequence:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-login-config.ini> -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_probe_login_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-login-config.ini> -ConnectHost <login-host> -ConnectPort 9100 -ExpectedWorldCount 0 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_world_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath <deploy-world-config.ini> -DryRun
```

When starting the login server without a test account password, add
`-SkipAccountPreflight` to the start wrapper. The long-running server still
uses the DB password. The wrapper verifies DB/schema/opcodes/version ranges with
`eq2_login_server --check-login-db --skip-account-auth`, then `--serve` repeats
its own startup checks.

## Step 9: Open The Source2 Login Port For Real Clients

The verifier uses loopback, but real clients need inbound UDP on the configured
login port. Source2 also listens on TCP on the same port for world/interserver
registration. For the target template port `9100`, run these on the source2
server host:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_configure_login_firewall.ps1 -DryRun
```

If the dry run matches the intended login port and remote address policy, rerun
from an elevated PowerShell prompt without `-DryRun`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_configure_login_firewall.ps1
```

Use `-RemoteAddress <client-subnet-or-world-host>` to restrict the inbound rules
for private deployments. The equivalent manual commands are:

```powershell
New-NetFirewallRule -DisplayName "Source2 Login UDP 9100" -Direction Inbound -Action Allow -Protocol UDP -LocalPort 9100
New-NetFirewallRule -DisplayName "Source2 Login TCP 9100" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 9100
```

## Done Criteria

The blocker is resolved when all of these pass from the source2 host:

- `Test-NetConnection -ComputerName 192.168.1.243 -Port 3306`
- `eq2_login_server.exe --check-login-db ... --client-version 546` with `EQ2_LOGIN_USERNAME` and `EQ2_LOGIN_PASSWORD` set.
- `eq2_world_server.exe --check-world-db ...`
- `source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify`
- `source2_live_login_verify.ps1 ... -RunMariaDbSmoke -RunServeProbe`, if you also want the lower-level verifier command.
- `ctest --test-dir build\source2-vcpkg-user-verify -C Debug -R source2_target_live_login_ctest --output-on-failure` with `EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN=1`
- `source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe`, once a valid `login_worldservers` account/password is available.
- `source2_live_login_verify.ps1 ... -RunWorldServe ... -ExpectedWorldCount 1`, if you also want the lower-level world-list verifier command.
- Source2 host firewall allows inbound UDP on the configured login port for clients and TCP on the same port for world registration.
