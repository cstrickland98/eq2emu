# Phase 0: Baseline Packet Audit

Status: blocked.

## Purpose

Establish a factual baseline for the current source2 login failure and capture
the original login server behavior that source2 must match.

## Where Work Begins

Start from:

- Current source2 login server.
- Original `source/LoginServer` and `source/common/EQStream.*`.
- Packet captures under `E:\_EQ2\packets`.
- Current source2 login server guide and probe scripts.

## Work Entailed

- Record the current source2 build, config, DB opcode lookup, and probe result.
- Record the real-client source2 failure packet sequence.
- Record the real-client original-source success packet sequence.
- Identify the first packet-level divergence and the original code path that
  handles it.
- Preserve packet payload hex fixtures that can be replayed by tests.
- Document whether source2 is failing before session, during key negotiation,
  during login request processing, or during post-login flow.

## Deliverables

- Packet-baseline notes added to this phase under `Progress`.
- Stored fixtures or fixture references for the source2 and original packet
  sequences.
- A short list of confirmed non-issues, such as firewall, `cl_ls_address`, DB
  opcode lookup, and basic UDP reachability when evidence supports them.

## Exit Criteria

- The first source2/original behavior divergence is identified by packet number,
  payload shape, and original source code path.
- The baseline has enough evidence for Phase 2 and Phase 3 tests to replay the
  failing client sequence.
- Current verification commands and results are recorded.

## Progress

Local phase evidence is complete, but the phase cannot be closed until its git
commit is made.

- Added `packet_baseline.md` with packet fixture references and the key payload
  hex from both captures.
- Confirmed source2 receives the real client and reaches the key negotiation
  path; the failure is not firewall, `cl_ls_address`, or UDP reachability.
- Confirmed the target login DB opcode lookup includes `OP_WSLoginRequestMsg`
  as `key_request_opcode=2`.
- Identified the first meaningful divergence:
  - source2 stops after ACKing the 113-byte client `OP_Packet`,
  - the original server continues by sending an encrypted sequenced packet,
  - the 113-byte packet is an embedded `OP_AppCombined` containing both RSA key
    material and encrypted `OP_LoginRequestMsg`.
- Identified the original source path:
  - `EQStream::ProcessPacket`, `OP_Packet`,
  - `EQStream::HandleEmbeddedPacket`,
  - `OP_AppCombined`,
  - `processRSAKey`,
  - `ProcessEmbeddedPacket`,
  - `ProcessEncryptedData`,
  - `Client::Process`, `OP_LoginRequestMsg`.
- Identified the current source2 gap:
  - `StreamPipeline::handle_encryption_key_packet` treats the whole 113-byte
    packet as key material and does not continue to the encrypted login
    subpacket.
- Current DB opcode verification:

```text
login-db-check account_auth=skipped request_opcode=0 reply_opcode=4 world_list_opcode=8 all_worlds_request_opcode=7 characters_request_opcode=9 characters_reply_opcode=10 key_request_opcode=2
```

## Verification Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
build\source2-vcpkg-user-verify\source2\apps\Debug\eq2_login_server.exe --check-login-db --skip-account-auth --config artifacts\source2-deploy-config\login_server.eq2emu-target.ini
```

## Git Commit

After this phase is complete, commit the changes:

```powershell
git add migrationv3 source2 docs scripts
git commit -m "Document login parity packet baseline"
```
