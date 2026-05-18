# Migration V3 Phase Commit Plan

Status: blocked by repository permissions.

## Purpose

Record the phase commit sequence required to close migrationv3 after the local
`.git` permission issue is fixed. The current shell cannot create
`.git/index.lock` or write `.git/objects`, so these commits must be made from a
process that can write the repository metadata.

Do not record or commit passwords, deploy configs with real secrets, packet
captures, or generated Python cache files.

## Preflight

Run these before staging commits:

```powershell
git status --short
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir build\source2-vcpkg-user-verify
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_run_target_live_login_gate.ps1 -BuildDir build\source2-vcpkg-user-verify -RunWorldServe -ExpectedWorldCount 1 -NoTranscript
```

The default CI should pass with the opt-in target CTest skipped by design. The
target live gate should be run separately with the required environment secrets.

Exclude these generated or local-only paths unless they are intentionally
reviewed and added in a later migration:

```text
artifacts/**
scripts/__pycache__/**
*.pyc
```

After `.git` permissions are fixed, the path-based sequence below can be run
with the helper instead of copying each command manually:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_commit_migrationv3_phases.ps1 -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_commit_migrationv3_phases.ps1
```

The helper defaults to Phase 0 through Phase 8. Phase 9 is guarded and requires
`-IncludePhase9` because it must only be committed after real-client acceptance
evidence is complete:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\source2_commit_migrationv3_phases.ps1 -IncludePhase9 -DryRun
```

## Commit Sequence

Phase 0:

```powershell
git add -- migrationv3\README.md migrationv3\phase_0_baseline_packet_audit.md migrationv3\packet_baseline.md
git commit -m "Document login parity packet baseline"
```

Phase 1:

```powershell
git add -- migrationv3\phase_1_legacy_login_inventory.md migrationv3\legacy_scenario_inventory.md
git commit -m "Inventory legacy login server parity scope"
```

Phase 2:

```powershell
git add -- migrationv3\phase_2_loginstream_transport_parity.md cmake\Eq2Dependencies.cmake source2\net\include\eq2\net\session.h source2\net\include\eq2\net\socket_transport.h source2\net\tests\net_tests.cpp source2\protocol\CMakeLists.txt source2\protocol\include\eq2\protocol\interserver_packet.h source2\protocol\include\eq2\protocol\stream_pipeline.h source2\protocol\tests\protocol_tests.cpp
git commit -m "Bring LoginStream transport behavior toward legacy parity"
```

Phase 3:

```powershell
git add -- migrationv3\phase_3_encrypted_login_handshake_parity.md source2\apps\login_server\main.cpp source2\login\tests\login_server_tests.cpp source2\protocol\include\eq2\protocol\login_response.h source2\protocol\tests\protocol_tests.cpp
git commit -m "Match legacy encrypted login handshake"
```

Phase 4:

```powershell
git add -- migrationv3\phase_4_account_login_policy_parity.md source2\db\include\eq2\db\fake_database.h source2\db\include\eq2\db\repositories.h source2\db\include\eq2\db\sql_repositories.h source2\db\tests\db_tests.cpp source2\login\include\eq2\login\live_login.h source2\login\tests\login_server_tests.cpp source2\protocol\include\eq2\protocol\login_request.h
git commit -m "Match legacy login account policy"
```

Phase 5:

```powershell
git add -- migrationv3\phase_5_world_registration_list_parity.md scripts\source2_live_login_verify.ps1 source2\apps\login_server\main.cpp source2\login\include\eq2\login\world_registration.h source2\login\tests\login_server_tests.cpp source2\protocol\include\eq2\protocol\login_world.h source2\world\include\eq2\world\live_world.h source2\world\tests\world_session_tests.cpp
git commit -m "Match legacy world registration and list behavior"
```

Phase 6:

```powershell
git add -- migrationv3\phase_6_character_lifecycle_parity.md source2\db\include\eq2\db\fake_database.h source2\db\include\eq2\db\repositories.h source2\db\include\eq2\db\sql_repositories.h source2\db\tests\db_tests.cpp source2\login\include\eq2\login\live_login.h source2\login\tests\login_server_tests.cpp source2\protocol\include\eq2\protocol\create_character.h source2\protocol\include\eq2\protocol\login_world.h source2\protocol\tests\protocol_tests.cpp
git commit -m "Match legacy character lifecycle login behavior"
```

Phase 7:

```powershell
git add -- migrationv3\phase_7_client_logs_admin_web_parity.md docs\eq2_2006_client_packet_logger_design.md docs\eq2_2006_client_packet_reversing_notes.md scripts\analyze_eq2_packet_log.py scripts\decode_eq2_packet_log.py scripts\extract_eq2_client_packet_registry.py scripts\frida_eq2_2006_packet_logger.js source2\protocol\include\eq2\protocol\client_log.h
git commit -m "Match legacy login admin and client log behavior"
```

Phase 8:

```powershell
git add -- migrationv3\phase_8_observability_and_diagnostics.md docs\source2_login_server_guide.md source2\apps\login_server\main.cpp source2\login\include\eq2\login\live_login.h source2\login\tests\login_server_tests.cpp
git commit -m "Add login parity diagnostics"
```

Phase 9 can only be committed after the real-client acceptance gates pass or
the remaining differences are explicitly approved:

```powershell
git add -- .gitignore migrationv3\phase_9_full_live_acceptance_gate.md migrationv3\phase_9_known_issues_and_recommendation.md migrationv3\packet_diagnostic_comparison.md migrationv3\real_client_acceptance_report.md migrationv3\completion_audit.md migrationv3\commit_status.md migrationv3\phase_commit_plan.md migrationv3\git_permission_recovery.md docs\source2_login_server_guide.md scripts\source2_run_real_client_acceptance_session.ps1 scripts\source2_run_real_client_acceptance_gate.ps1 scripts\source2_set_eq2_client_login.ps1 scripts\source2_prepare_eq2_client_sandbox.ps1 scripts\source2_record_real_client_acceptance_results.ps1 scripts\source2_audit_real_client_acceptance_session.ps1 scripts\source2_audit_migrationv3_completion.ps1 scripts\source2_diagnose_git_permissions.ps1 scripts\source2_commit_migrationv3_phases.ps1 scripts\source2_check_eq2_client_directx.ps1 scripts\source2_capture_login_packets.ps1 source2\apps\CMakeLists.txt
git commit -m "Accept source2 login server parity"
```

## Notes On Cross-Phase Files

Several files contain changes that support more than one phase:

- `source2/apps/login_server/main.cpp`
- `source2/login/include/eq2/login/live_login.h`
- `source2/login/tests/login_server_tests.cpp`
- `source2/protocol/tests/protocol_tests.cpp`
- `source2/db/include/eq2/db/fake_database.h`
- `source2/db/include/eq2/db/repositories.h`
- `source2/db/include/eq2/db/sql_repositories.h`
- `source2/db/tests/db_tests.cpp`

If strict phase isolation is required, use hunk staging from a normal
interactive shell after fixing `.git` permissions. If the priority is closing
the migration with auditable phase messages, use the path-based sequence above
and keep this file with the commit history as the recovery record.
