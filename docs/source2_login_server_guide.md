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
opcode_width = 1

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
server. For another PC on the LAN, use the source2 host's LAN IP. For internet
clients, use the public DNS name or public IP that forwards UDP `9100` to the
source2 host. Keep the login server on port `9100` unless your client profile or
launcher supports an explicit login-port override.

Start the client and log in with an account from `eq2ls.account`. If the client
reaches login but shows no worlds, the login server is reachable and no world
server is currently registered.

To advertise a world, configure
`artifacts\source2-deploy-config\world_server.eq2emu-target.ini` with a
client-reachable `advertised_address`, set its world `account` and `password` to
match a row in `eq2ls.login_worldservers`, and set `[login] remote_address` /
`remote_port` to the running login server. Then start the world wrapper:

```powershell
$env:EQ2_DB_PASSWORD = "<db-password>"
$env:EQ2_WORLD_ACCOUNT = "<world-account>"
$env:EQ2_WORLD_PASSWORD = "<world-password>"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_start_world_server.ps1 -BuildDir build\source2-vcpkg-user-verify -ConfigPath artifacts\source2-deploy-config\world_server.eq2emu-target.ini
```

Rerun the login probe with `-ExpectedWorldCount 1` after the world registers.
