# Phase 7: Login Vertical Slice

Status: complete.

## Purpose

Rebuild login first because it has clearer boundaries than world and zone simulation.

## Scope

Create a source2 login server path that uses source2 protocol, net, db, and core modules.

## Deliverables

- Login app wiring.
- Login server config loading.
- Account authentication through `eq2::db`.
- Login packet handling through `eq2::protocol`.
- Connection handling through `eq2::net`.
- World registration handling.

## Progress

- Added `eq2::login::LoginServerConfig` and config loading from `eq2::core::ConfigProvider`.
- Added `eq2::login::LoginServer` as the source2 login vertical slice composition point.
- Wired login sessions through `eq2::net::TcpServer` and records inbound login bytes as net receive events.
- Parses legacy login request payloads through `eq2::protocol::parse_legacy_login_request`.
- Authenticates accounts through the `eq2::db::LoginAccountRepository` contract and source2 fake DB implementation.
- Handles login/world registration packets through source2 protocol framing and `eq2::db::WorldRegistrationRepository`.
- Updated the `eq2_login_server` app target to load source2 login config and report the configured endpoint.
- Added `eq2_login_server --smoke-login`, which runs the source2 login app target through a legacy login byte fixture and verifies the accepted reply path.
- Added `eq2_login_server_tests` covering config loading, valid legacy login success, bad-version rejection, and source2 world registration handling.
- Existing-client parity is represented by the legacy login packet byte fixture and legacy reply-code outcomes; the remaining temporary incompatibility is that the source2 app does not yet expose the full legacy EQStream/socket adapter needed for a live external client.

## Exit Criteria

- Existing client can reach the same login outcome as the legacy login server.
- Any temporary incompatibility with the legacy world server is explicitly documented.
- Login behavior is covered by characterization tests.
