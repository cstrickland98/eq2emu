# Phase 4: Account Login Policy Parity

Status: blocked.

## Purpose

Match original login request parsing and account policy behavior after the
client can complete encrypted login negotiation.

## Where Work Begins

Start after Phase 3 is complete. Use:

- `source/LoginServer/client.cpp`
- `source/LoginServer/LoginDatabase.cpp`
- `source/LoginServer/LoginAccount.cpp`
- `server/LoginStructs.xml`
- Source2 login authentication and DB repositories.

## Work Entailed

- Match legacy `LS_LoginRequest` parsing for classic and newer client-version
  packet layouts.
- Match supported-version and opcode-version decisions.
- Match login accepted, invalid credentials, bad version, already-playing, and
  unknown rejection response payloads.
- Match account creation behavior when enabled and disabled.
- Match duplicate-session handling and old-client kick behavior.
- Match account IP and client-data-version DB updates.
- Match banned account/IP behavior if present in the original DB paths.
- Match account bonus fields needed by subsequent character-list responses.

## Deliverables

- Account/auth policy parity implementation.
- DB repository methods for every original account read/write used by login.
- Tests for every login response code and policy branch.
- Live DB smoke covering accepted login, failed login, and bad-version login.

## Exit Criteria

- Source2 login responses match original behavior for all characterized account
  policy branches.
- Account creation and duplicate-session behavior either match original or have
  an explicitly reviewed compatibility decision.
- DB writes made by original login are present in source2 where still relevant.

## Progress

- Existing characterization coverage confirms accepted login, invalid password,
  bad client version, account creation enabled/disabled, and duplicate-session
  handling.
- Added source2 account-login side effects matching the original
  `LoginDatabase::UpdateAccountIPAddress` and
  `LoginDatabase::UpdateAccountClientDataVersion` calls:
  - live TCP/UDP sessions now carry the remote address into login service state,
  - SQL-backed successful login records `account.ip_address`,
  - SQL-backed successful login records `account.last_client_version`.
- Added tests proving loopback remote address capture and successful login DB
  update queries.
- Corrected the classic `LS_LoginRequest` parser to match the original
  ClientVersion 1 structure from `server\LoginStructs.xml`: two string fields,
  username/password, `acctNum`, `passCode`, then the 16-bit client version.
  The previous parser required two unused trailing `int32` fields after the
  version and rejected the real 546 client login payload.
- Added protocol regression coverage for the decrypted real-client 546 payload
  from `E:\_EQ2\packets\source2hex.txt`.
- Reviewed the original `LoginDatabase::LoadAccount` path. It only checks
  `account.name` plus `passwd=sha2(...)`, optionally creates the account, and
  does not contain a separate login-account ban or account-status branch. The
  `login_bannedips` table in the original source is used by world registration,
  which is covered in Phase 5.
- Verified current account-policy-sensitive tests:

```text
cmake --build build\source2-vcpkg-user-verify --target eq2_net_tests eq2_login_server_tests eq2_db_tests eq2_login_server --config Debug
build\source2-vcpkg-user-verify\source2\net\Debug\eq2_net_tests.exe
build\source2-vcpkg-user-verify\source2\login\Debug\eq2_login_server_tests.exe
build\source2-vcpkg-user-verify\source2\db\Debug\eq2_db_tests.exe
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-live
```

Observed result:

- `smoke-login-live accepted account=42 worlds=0`
- Exact captured-client replay after the parser fix logged
  `login_accepted`, `client_version=546`, `login_attempts=1`,
  `malformed_packets=0`, and `unsupported_packets=0`.

Remaining before completion:

- Confirm the real-client target login updates `account.ip_address` and
  `account.last_client_version` through MariaDB.
- Commit phase changes once the local `.git` permission issue is resolved.

## Verification Commands

```powershell
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --smoke-login-mariadb --config artifacts\source2-deploy-config\login_server.eq2emu-target.ini --username <login-user> --password <login-password> --expect-world-count 0
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add source2 scripts docs migrationv3
git commit -m "Match legacy login account policy"
```
