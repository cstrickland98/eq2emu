add_executable(eq2_login_server
  login_server/main.cpp
)

target_link_libraries(eq2_login_server PRIVATE
  eq2::login
)

eq2_target_defaults(eq2_login_server)

add_executable(eq2_world_server
  world_server/main.cpp
)

target_link_libraries(eq2_world_server PRIVATE
  eq2::world
  eq2::login
)

eq2_target_defaults(eq2_world_server)

if(BUILD_TESTING)
  if(WIN32)
    find_program(EQ2_POWERSHELL_EXE powershell)
    if(EQ2_POWERSHELL_EXE)
      foreach(script_name IN ITEMS source2_check_mariadb_tcp.ps1 source2_live_login_verify.ps1 source2_owner_live_login_preflight.ps1 source2_target_live_login_ctest.ps1 source2_run_target_live_login_gate.ps1 source2_start_login_server.ps1 source2_probe_login_server.ps1 source2_start_world_server.ps1 source2_run_real_client_acceptance_session.ps1 source2_run_real_client_acceptance_gate.ps1 source2_set_eq2_client_login.ps1 source2_prepare_eq2_client_sandbox.ps1 source2_record_real_client_acceptance_results.ps1 source2_audit_real_client_acceptance_session.ps1 source2_audit_migrationv3_completion.ps1 source2_diagnose_git_permissions.ps1 source2_commit_migrationv3_phases.ps1 source2_configure_login_firewall.ps1 source2_prepare_deploy_config.ps1 source2_check_eq2_client_directx.ps1 source2_capture_login_packets.ps1)
        add_test(NAME ${script_name}_parses
          COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                  -Command "$errors = $null; [System.Management.Automation.PSParser]::Tokenize((Get-Content '${CMAKE_SOURCE_DIR}/scripts/${script_name}' -Raw), [ref]$errors) | Out-Null; if ($errors) { $errors | Format-List; exit 1 }"
        )
      endforeach()

      add_test(NAME source2_prepare_deploy_config_dry_run_lists_login_and_world
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_prepare_deploy_config.ps1' -OutputDir '${CMAKE_BINARY_DIR}/deploy config dry run' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Login config:' -or $text -notmatch 'World config:' -or $text -notmatch 'login_server.eq2emu-target.ini.example' -or $text -notmatch 'world_server.eq2emu-target.ini.example' -or $text -notmatch 'placeholders are preserved') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_prepare_deploy_config_creates_placeholder_configs
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('deploy_config_test_' + [guid]::NewGuid().ToString('N')); $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_prepare_deploy_config.ps1' -OutputDir $dir *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $login = Join-Path $dir 'login_server.eq2emu-target.ini'; $world = Join-Path $dir 'world_server.eq2emu-target.ini'; if (!(Test-Path -LiteralPath $login) -or !(Test-Path -LiteralPath $world)) { $output | Write-Host; exit 1 }; $loginText = Get-Content -LiteralPath $login -Raw; $worldText = Get-Content -LiteralPath $world -Raw; if ($loginText -notmatch 'password = change-me' -or $worldText -notmatch 'account = change-me' -or $worldText -notmatch 'password = change-me') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_prepare_deploy_config_refuses_overwrite_without_force
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' 'deploy_config_overwrite_test'; New-Item -ItemType Directory -Force -Path $dir | Out-Null; $login = Join-Path $dir 'login_server.eq2emu-target.ini'; Set-Content -LiteralPath $login -Value 'existing'; $output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_prepare_deploy_config.ps1' -Kind Login -OutputDir $dir *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'already exists') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_configure_login_firewall_dry_run_defaults
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_configure_login_firewall.ps1' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Source2 Login UDP 9100' -or $text -notmatch 'Source2 Login TCP 9100' -or $text -notmatch 'New-NetFirewallRule' -or $text -notmatch '-Protocol UDP' -or $text -notmatch '-Protocol TCP' -or $text -notmatch '-LocalPort 9100' -or $text -notmatch '-RemoteAddress Any') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_configure_login_firewall_dry_run_quotes_names
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_configure_login_firewall.ps1' -RulePrefix 'Source2 Login Test' -LoginPort 9200 -RemoteAddress '192.168.1.0/24' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '-DisplayName ''Source2 Login Test UDP 9200''' -or $text -notmatch '-DisplayName ''Source2 Login Test TCP 9200''' -or $text -notmatch '-RemoteAddress 192.168.1.0/24') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_configure_login_firewall_rejects_invalid_port
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_configure_login_firewall.ps1' -LoginPort 0 -DryRun *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'LoginPort must be an integer from 1 to 65535') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_owner_live_login_preflight_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/source2_owner_live_login_preflight.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -DryRun
      )

      add_test(NAME source2_owner_live_login_preflight_world_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/source2_owner_live_login_preflight.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -RunWorldServe
                -DryRun
      )

      add_test(NAME source2_target_live_login_ctest
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/source2_target_live_login_ctest.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
      )
      set_tests_properties(source2_target_live_login_ctest
        PROPERTIES SKIP_REGULAR_EXPRESSION "^SKIP:"
      )

      add_test(NAME source2_run_target_live_login_gate_dry_run_sets_ctest_env
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'source2_check_mariadb_tcp.ps1' -or $text -notmatch 'EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN' -or $text -notmatch 'EQ2_SOURCE2_TARGET_DB_HOST' -or $text -notmatch 'ctest --test-dir' -or $text -notmatch 'source2_target_live_login_ctest') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_run_target_live_login_gate_world_dry_run_sets_world_env
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -RunWorldServe -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2_SOURCE2_RUN_TARGET_WORLD_LIST' -or $text -notmatch 'EQ2_WORLD_ACCOUNT' -or $text -notmatch '-RunWorldServe') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_run_target_live_login_gate_checks_tcp_before_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -Raw; $tcp = $text.IndexOf('& powershell -NoProfile -ExecutionPolicy Bypass -File $tcpScript'); $capture = $text.IndexOf('$exitCode = $LASTEXITCODE', $tcp); $secret = $text.IndexOf('Require-EnvSecret \"EQ2_DB_PASSWORD\"'); if ($tcp -lt 0 -or $capture -lt 0 -or $secret -lt 0 -or $tcp -gt $secret -or $capture -gt $secret) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_run_target_live_login_gate_continues_after_tcp_success
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('target_gate_continue_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $tcp = Join-Path $dir 'tcp_ok.ps1'; $fakeCtest = Join-Path $dir 'fake_ctest.ps1'; Set-Content -LiteralPath $tcp -Value @('param([string]$DbHost,[int]$DbPort)', 'Write-Host (''fake tcp ok '' + $DbHost + '':'' + $DbPort)', 'exit 0'); Set-Content -LiteralPath $fakeCtest -Value @('param([Parameter(ValueFromRemainingArguments=$true)][string[]]$Args)', 'Write-Host (''fake ctest args: '' + ($Args -join '' ''))'); $previousDb = $env:EQ2_DB_PASSWORD; $previousLogin = $env:EQ2_LOGIN_PASSWORD; try { $env:EQ2_DB_PASSWORD = 'fake-db-secret'; $env:EQ2_LOGIN_PASSWORD = 'fake-login-secret'; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -TcpScriptPath $tcp -CtestExe $fakeCtest -NoTranscript *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -ne 0 -or $text -notmatch 'fake tcp ok' -or $text -notmatch '== Running CTest target live login gate ==' -or $text -notmatch 'source2 target live login gate passed.') { $output | Write-Host; exit 1 } } finally { $env:EQ2_DB_PASSWORD = $previousDb; $env:EQ2_LOGIN_PASSWORD = $previousLogin }"
      )

      add_test(NAME source2_run_target_live_login_gate_sets_target_env_before_ctest
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -Raw; $set = $text.IndexOf('Set-Item -Path \"Env:$($entry.Key)\"'); $ctest = $text.IndexOf('& $CtestExe @ctestArgs'); $restore = $text.LastIndexOf('Remove-Item -Path \"Env:$($entry.Key)\"'); if ($set -lt 0 -or $ctest -lt 0 -or $restore -lt 0 -or $set -gt $ctest -or $restore -lt $ctest) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_run_target_live_login_gate_exits_after_env_restore
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -Raw; $ctest = $text.IndexOf('& $CtestExe @ctestArgs'); $capture = $text.IndexOf('$exitCode = $LASTEXITCODE', $ctest); $restore = $text.LastIndexOf('$env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN = $previousRunTarget'); $stopTranscript = $text.LastIndexOf('Stop-GateTranscript'); $exit = $text.LastIndexOf('exit $exitCode'); if ($ctest -lt 0 -or $capture -lt 0 -or $restore -lt 0 -or $stopTranscript -lt 0 -or $exit -lt 0 -or $capture -gt $restore -or $exit -lt $stopTranscript) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_run_target_live_login_gate_prints_success_on_ctest_pass
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -Raw; $ctest = $text.IndexOf('& $CtestExe @ctestArgs'); $success = $text.IndexOf('source2 target live login gate passed.'); $restore = $text.LastIndexOf('$env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN = $previousRunTarget'); if ($ctest -lt 0 -or $success -lt 0 -or $restore -lt 0 -or $success -lt $ctest -or $success -gt $restore) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_run_target_live_login_gate_transcripts_non_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -Raw; $start = $text.IndexOf('Start-Transcript -Path $transcriptPath -Force'); $stop = $text.LastIndexOf('Stop-GateTranscript'); $dryRun = $text.IndexOf('if (!$DryRun -and !$NoTranscript)'); if ($start -lt 0 -or $stop -lt 0 -or $dryRun -lt 0 -or $dryRun -gt $start -or $stop -lt $start) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_target_live_login_ctest_reads_env_overrides
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_target_live_login_ctest.ps1' -Raw; $override = $text.IndexOf('EQ2_SOURCE2_TARGET_DB_HOST'); $params = $text.IndexOf('$verifyParams = @{'); if ($override -lt 0 -or $params -lt 0 -or $override -gt $params -or $text -notmatch 'EQ2_SOURCE2_TARGET_EXPECTED_WORLD_COUNT' -or $text -notmatch 'EQ2_SOURCE2_TARGET_WORLD_SERVER_VERSION') { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_target_live_login_ctest_runs_smoke_and_probe
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_target_live_login_ctest.ps1' -Raw; $params = $text.IndexOf('$verifyParams = @{'); $invoke = $text.IndexOf('& $verifyScript @verifyParams'); $smoke = $text.IndexOf('RunMariaDbSmoke = $true', $params); $probe = $text.IndexOf('RunServeProbe = $true', $params); if ($params -lt 0 -or $invoke -lt 0 -or $smoke -lt 0 -or $probe -lt 0 -or $smoke -gt $invoke -or $probe -gt $invoke) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_owner_live_login_preflight_dry_run_quotes_spaced_args
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_owner_live_login_preflight.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -WorldName 'Source2 World' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '-WorldName ''Source2 World''') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_owner_live_login_preflight_dry_run_uses_execution_policy_bypass
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_owner_live_login_preflight.ps1' -BuildDir 'build source2 spaced' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '(?m)^powershell -NoProfile -ExecutionPolicy Bypass -File ' -or $text -notmatch '-BuildDir ''build source2 spaced''') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_owner_live_login_preflight_dry_run_labels_login_gate
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_owner_live_login_preflight.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Gate: login-only') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_owner_live_login_preflight_world_dry_run_labels_world_gate
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_owner_live_login_preflight.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -RunWorldServe -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Gate: world-list') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_owner_wrappers_dry_run_redact_env_secret_values
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$ErrorActionPreference = 'Stop'; $secrets = @('dry-run-db-secret-value', 'dry-run-login-secret-value', 'dry-run-world-account-value', 'dry-run-world-secret-value'); $previousDb = $env:EQ2_DB_PASSWORD; $previousLogin = $env:EQ2_LOGIN_PASSWORD; $previousWorldAccount = $env:EQ2_WORLD_ACCOUNT; $previousWorldPassword = $env:EQ2_WORLD_PASSWORD; try { $env:EQ2_DB_PASSWORD = $secrets[0]; $env:EQ2_LOGIN_PASSWORD = $secrets[1]; $env:EQ2_WORLD_ACCOUNT = $secrets[2]; $env:EQ2_WORLD_PASSWORD = $secrets[3]; $output = @(); $output += & '${CMAKE_SOURCE_DIR}/scripts/source2_owner_live_login_preflight.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -RunWorldServe -DryRun *>&1; $output += & '${CMAKE_SOURCE_DIR}/scripts/source2_run_target_live_login_gate.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -RunWorldServe -DryRun *>&1; $output += & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -RunForMs 1 -DryRun *>&1; $output += & '${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -DryRun *>&1; $output += & '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -RunForMs 1 -DryRun *>&1; $text = [string]::Join([Environment]::NewLine, $output); foreach ($secret in $secrets) { if ($text -match [regex]::Escape($secret)) { $output | Write-Host; exit 1 } }; if ($text -notmatch 'EQ2_DB_PASSWORD' -or $text -notmatch 'EQ2_LOGIN_PASSWORD' -or $text -notmatch 'EQ2_WORLD_ACCOUNT' -or $text -notmatch 'EQ2_WORLD_PASSWORD') { $output | Write-Host; exit 1 } } finally { $env:EQ2_DB_PASSWORD = $previousDb; $env:EQ2_LOGIN_PASSWORD = $previousLogin; $env:EQ2_WORLD_ACCOUNT = $previousWorldAccount; $env:EQ2_WORLD_PASSWORD = $previousWorldPassword }"
      )

      add_test(NAME source2_live_login_verify_clears_login_env_before_serve
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_live_login_verify.ps1' -Raw; $clearUser = $text.IndexOf('$env:EQ2_LOGIN_USERNAME = $null'); $clearPassword = $text.IndexOf('$env:EQ2_LOGIN_PASSWORD = $null'); $serve = $text.IndexOf('Start-Process -FilePath $loginServer'); $probe = $text.IndexOf('Probe running login server'); if ($clearUser -lt 0 -or $clearPassword -lt 0 -or $serve -lt 0 -or $probe -lt 0 -or $clearUser -gt $serve -or $clearPassword -gt $serve -or $probe -lt $serve) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_live_login_verify_scopes_world_env_to_world_serve
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_live_login_verify.ps1' -Raw; $loginServe = $text.IndexOf('Start-Process -FilePath $loginServer'); $worldSet = $text.IndexOf('$env:EQ2_WORLD_ACCOUNT = $WorldAccount', $loginServe); $worldServe = $text.IndexOf('Start-Process -FilePath $worldServer'); $worldClear = $text.IndexOf('$env:EQ2_WORLD_ACCOUNT = $null', $worldServe); if ($loginServe -lt 0 -or $worldSet -lt 0 -or $worldServe -lt 0 -or $worldClear -lt 0 -or $worldSet -gt $worldServe -or $worldClear -lt $worldServe) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_start_login_server_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -ConfigPath "${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example"
                -RunForMs 1
                -DryRun
      )

      add_test(NAME source2_start_login_server_dry_run_lists_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2_DB_PASSWORD' -or $text -notmatch 'EQ2_LOGIN_PASSWORD' -or $text -notmatch 'Login username: testlabs' -or $text -notmatch 'Login DB/account/opcode check' -or $text -notmatch '--check-login-db') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_login_server_skip_preflight_dry_run_lists_only_runtime_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -SkipDbPreflight -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2_DB_PASSWORD' -or $text -match 'EQ2_LOGIN_PASSWORD' -or $text -notmatch 'Planned preflight: skipped by -SkipDbPreflight') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_login_server_skip_account_preflight_keeps_db_secret_only
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -SkipAccountPreflight -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2_DB_PASSWORD' -or $text -match 'EQ2_LOGIN_PASSWORD' -or $text -notmatch 'Login DB/opcode check' -or $text -notmatch '--skip-account-auth') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_login_server_skip_account_preflight_checks_db_opcodes
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -Raw; $label = $text.IndexOf('Login DB/opcode check'); $flag = $text.IndexOf('--skip-account-auth'); $serve = $text.IndexOf('== Starting source2 login server =='); if ($label -lt 0 -or $flag -lt 0 -or $serve -lt 0 -or $flag -gt $serve) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_start_login_server_config_password_satisfies_skip_account_preflight
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$cfg = Join-Path '${CMAKE_BINARY_DIR}' 'login_config_secret_skip_account_test.ini'; (Get-Content '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -Raw).Replace('password = change-me', 'password = configured-db-secret') | Set-Content -LiteralPath $cfg; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath $cfg -SkipAccountPreflight -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -match 'EQ2_DB_PASSWORD' -or $text -match 'EQ2_LOGIN_PASSWORD' -or $text -notmatch '\\(none\\)') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_login_server_config_password_satisfies_runtime_secret
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$cfg = Join-Path '${CMAKE_BINARY_DIR}' 'login_config_secret_test.ini'; (Get-Content '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -Raw).Replace('password = change-me', 'password = configured-db-secret') | Set-Content -LiteralPath $cfg; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath $cfg -SkipDbPreflight -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -match 'EQ2_DB_PASSWORD' -or $text -match 'EQ2_LOGIN_PASSWORD' -or $text -notmatch '\\(none\\)') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_login_server_clears_env_when_config_supplies_db_secret
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -Raw; $source = $text.IndexOf('if ($DbPasswordSecret.Source -ne \"config\")'); $clear = $text.IndexOf('$env:EQ2_DB_PASSWORD = $null', $source); $serve = $text.IndexOf('== Starting source2 login server =='); if ($source -lt 0 -or $clear -lt 0 -or $serve -lt 0 -or $clear -gt $serve) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_start_login_server_dry_run_quotes_spaced_args
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -DbName 'eq2 ls' -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '--db-name ''eq2 ls''') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_login_server_dry_run_uses_call_operator
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '(?m)^& ') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_login_server_clears_login_env_before_serve
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -Raw; $clearUser = $text.IndexOf('$env:EQ2_LOGIN_USERNAME = $null'); $clearPassword = $text.IndexOf('$env:EQ2_LOGIN_PASSWORD = $null'); $serve = $text.IndexOf('== Starting source2 login server =='); if ($clearUser -lt 0 -or $clearPassword -lt 0 -or $serve -lt 0 -or $clearUser -gt $serve -or $clearPassword -gt $serve) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_start_login_server_checks_db_tcp_before_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_start_login_server.ps1' -Raw; $tcp = $text.IndexOf('Test-NetConnection -ComputerName $DbHost -Port $DbPort'); $dbSecret = $text.IndexOf('$DbPasswordSecret = Resolve-SecretOrConfig'); $loginSecret = $text.IndexOf('$LoginPassword = Resolve-Secret'); if ($tcp -lt 0 -or $dbSecret -lt 0 -or $loginSecret -lt 0 -or $tcp -gt $dbSecret -or $tcp -gt $loginSecret) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_probe_login_server_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -ConfigPath "${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example"
                -DryRun
      )

      add_test(NAME source2_probe_login_server_dry_run_lists_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2_DB_PASSWORD' -or $text -notmatch 'EQ2_LOGIN_PASSWORD' -or $text -notmatch 'Login username: testlabs') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_probe_login_server_config_password_satisfies_db_secret
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$cfg = Join-Path '${CMAKE_BINARY_DIR}' 'probe_config_secret_test.ini'; (Get-Content '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -Raw).Replace('password = change-me', 'password = configured-db-secret') | Set-Content -LiteralPath $cfg; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath $cfg -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -match 'EQ2_DB_PASSWORD' -or $text -notmatch 'EQ2_LOGIN_PASSWORD') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_probe_login_server_clears_env_when_config_supplies_db_secret
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1' -Raw; $source = $text.IndexOf('if ($DbPasswordSecret.Source -ne \"config\")'); $clear = $text.IndexOf('$env:EQ2_DB_PASSWORD = $null', $source); $probe = $text.IndexOf('== Probing source2 login server =='); if ($source -lt 0 -or $clear -lt 0 -or $probe -lt 0 -or $clear -gt $probe) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_probe_login_server_dry_run_quotes_spaced_args
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -DbName 'eq2 ls' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '--db-name ''eq2 ls''') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_probe_login_server_dry_run_uses_call_operator
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '(?m)^& ') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_probe_login_server_checks_db_tcp_before_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_probe_login_server.ps1' -Raw; $tcp = $text.IndexOf('Test-NetConnection -ComputerName $DbHost -Port $DbPort'); $dbSecret = $text.IndexOf('$DbPasswordSecret = Resolve-SecretOrConfig'); $loginSecret = $text.IndexOf('$LoginPassword = Resolve-Secret'); if ($tcp -lt 0 -or $dbSecret -lt 0 -or $loginSecret -lt 0 -or $tcp -gt $dbSecret -or $tcp -gt $loginSecret) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_start_world_server_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -File "${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1"
                -BuildDir "${CMAKE_BINARY_DIR}"
                -ConfigPath "${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example"
                -RunForMs 1
                -DryRun
      )

      add_test(NAME source2_start_world_server_dry_run_lists_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2_DB_PASSWORD' -or $text -notmatch 'EQ2_WORLD_ACCOUNT' -or $text -notmatch 'EQ2_WORLD_PASSWORD') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_world_server_skip_preflights_dry_run_lists_runtime_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -SkipDbPreflight -SkipLoginReachability -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2_DB_PASSWORD' -or $text -notmatch 'EQ2_WORLD_ACCOUNT' -or $text -notmatch 'EQ2_WORLD_PASSWORD') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_world_server_config_secrets_satisfy_runtime_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$cfg = Join-Path '${CMAKE_BINARY_DIR}' 'world_config_secret_test.ini'; ((Get-Content '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -Raw) -replace 'account = change-me', 'account = configured-world' -replace 'password = change-me', 'password = configured-secret') | Set-Content -LiteralPath $cfg; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath $cfg -SkipDbPreflight -SkipLoginReachability -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -match 'EQ2_DB_PASSWORD' -or $text -match 'EQ2_WORLD_ACCOUNT' -or $text -match 'EQ2_WORLD_PASSWORD' -or $text -notmatch '\\(none\\)') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_world_server_clears_env_when_config_supplies_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -Raw; $dbSource = $text.IndexOf('if ($DbPasswordSecret.Source -ne \"config\")'); $dbClear = $text.IndexOf('$env:EQ2_DB_PASSWORD = $null', $dbSource); $accountSource = $text.IndexOf('if ($WorldAccountSecret.Source -ne \"config\")'); $accountClear = $text.IndexOf('$env:EQ2_WORLD_ACCOUNT = $null', $accountSource); $passwordSource = $text.IndexOf('if ($WorldPasswordSecret.Source -ne \"config\")'); $passwordClear = $text.IndexOf('$env:EQ2_WORLD_PASSWORD = $null', $passwordSource); $serve = $text.IndexOf('== Starting source2 world server =='); if ($dbSource -lt 0 -or $dbClear -lt 0 -or $accountSource -lt 0 -or $accountClear -lt 0 -or $passwordSource -lt 0 -or $passwordClear -lt 0 -or $serve -lt 0 -or $dbClear -gt $serve -or $accountClear -gt $serve -or $passwordClear -gt $serve) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_start_world_server_dry_run_quotes_spaced_args
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -DbName 'eq2 world' -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '--db-name ''eq2 world''') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_world_server_dry_run_uses_call_operator
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -RunForMs 1 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '(?m)^& ') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_start_world_server_checks_tcp_before_secrets
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$text = Get-Content '${CMAKE_SOURCE_DIR}/scripts/source2_start_world_server.ps1' -Raw; $tcp = $text.IndexOf('Test-NetConnection -ComputerName $DbHost -Port $DbPort'); $loginTcp = $text.IndexOf('Test-NetConnection -ComputerName $LoginHost -Port $LoginPort'); $dbSecret = $text.IndexOf('$DbPasswordSecret = Resolve-SecretOrConfig'); $worldAccount = $text.IndexOf('$WorldAccountSecret = Resolve-SecretOrConfig'); $worldPassword = $text.IndexOf('$WorldPasswordSecret = Resolve-SecretOrConfig'); if ($tcp -lt 0 -or $loginTcp -lt 0 -or $dbSecret -lt 0 -or $worldAccount -lt 0 -or $worldPassword -lt 0 -or $tcp -gt $dbSecret -or $loginTcp -gt $dbSecret -or $tcp -gt $worldAccount -or $loginTcp -gt $worldAccount -or $tcp -gt $worldPassword -or $loginTcp -gt $worldPassword) { Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_run_real_client_acceptance_session_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_real_client_acceptance_session.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -LoginConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -WorldConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -ClientTargetHost 192.168.1.41 -WorldName 'Source2 World' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'real-client acceptance session dry run' -or $text -notmatch 'cl_ls_address 192.168.1.41' -or $text -notmatch 'EQ2_DB_PASSWORD' -or $text -notmatch 'EQ2_WORLD_ACCOUNT' -or $text -notmatch 'EQ2_WORLD_PASSWORD' -or $text -notmatch '--diagnostic-events' -or $text -notmatch '--world-advertised-address 192.168.1.41' -or $text -notmatch '--world-name') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_run_real_client_acceptance_session_client_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('client_acceptance_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; Set-Content -LiteralPath (Join-Path $dir 'eq2_default.ini') -Value 'cl_ls_address 192.168.1.243'; Set-Content -LiteralPath (Join-Path $dir 'eq2_packet_log.jsonl') -Value '{}'; New-Item -ItemType File -Path (Join-Path $dir 'EverQuest2.exe') | Out-Null; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_real_client_acceptance_session.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -LoginConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -WorldConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -ClientDir $dir -ClientTargetHost 192.168.1.41 -UpdateClientConfig -LaunchClient -RestoreClientConfigOnExit -SnapshotClientLogs -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Planned client config update' -or $text -notmatch 'Planned client config restore on exit' -or $text -notmatch 'Planned client launch' -or $text -notmatch 'Planned client log snapshots' -or $text -notmatch 'eq2_packet_log.jsonl' -or $text -notmatch 'source2_set_eq2_client_login.ps1' -or $text -notmatch 'EverQuest2.exe') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_run_real_client_acceptance_session_auto_login_dry_run_redacts_password
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('client_acceptance_auto_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; Set-Content -LiteralPath (Join-Path $dir 'eq2_default.ini') -Value 'cl_ls_address 192.168.1.243'; New-Item -ItemType File -Path (Join-Path $dir 'EverQuest2.exe') | Out-Null; $oldPassword = $env:EQ2_LOGIN_PASSWORD; $env:EQ2_LOGIN_PASSWORD = 'supersecret-autologin-test'; try { $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_real_client_acceptance_session.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -LoginConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -WorldConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -ClientDir $dir -ClientTargetHost 127.0.0.1 -LaunchClient -AutoLoginClient -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Planned client auto-login: enabled' -or $text -notmatch 'password redacted' -or $text -match 'supersecret-autologin-test') { $output | Write-Host; exit 1 } } finally { $env:EQ2_LOGIN_PASSWORD = $oldPassword }"
      )

      add_test(NAME source2_run_real_client_acceptance_session_redacts_client_session_arg
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('client_acceptance_args_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; Set-Content -LiteralPath (Join-Path $dir 'eq2_default.ini') -Value 'cl_ls_address 192.168.1.243'; New-Item -ItemType File -Path (Join-Path $dir 'EverQuest2.exe') | Out-Null; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_real_client_acceptance_session.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -LoginConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -WorldConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -ClientDir $dir -ClientTargetHost 127.0.0.1 -LaunchClient -ClientArguments @('cl_username','testlabs','cl_sessionid','supersecret-session-token') -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch '<redacted>' -or $text -match 'supersecret-session-token') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_run_real_client_acceptance_gate_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_run_real_client_acceptance_gate.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -ClientDir 'E:/Games/Everquest II' -ClientTargetHosts 127.0.0.1,192.168.1.41 -UpdateClientConfig -LaunchClient -RestoreClientConfigOnExit -SnapshotClientLogs -ForbiddenSecret wrongpass -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Source2 real-client acceptance gate dry run' -or $text -notmatch 'Host: 127.0.0.1' -or $text -notmatch 'Host: 192.168.1.41' -or $text -notmatch 'source2_run_real_client_acceptance_session.ps1' -or $text -notmatch 'source2_record_real_client_acceptance_results.ps1' -or $text -notmatch 'source2_audit_real_client_acceptance_session.ps1' -or $text -notmatch '-RequireLoginAccepted' -or $text -notmatch '-RequireClientLogSnapshot' -or $text -notmatch '-ForbiddenSecret wrongpass') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_run_real_client_acceptance_session_rejects_too_short_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_run_real_client_acceptance_session.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -LoginConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -WorldConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -RunForMs 2000 -DryRun *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'at least 3000 milliseconds') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_run_real_client_acceptance_session_writes_failure_report
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$oldDb = $env:EQ2_DB_PASSWORD; $oldWorldAccount = $env:EQ2_WORLD_ACCOUNT; $oldWorldPassword = $env:EQ2_WORLD_PASSWORD; $env:EQ2_DB_PASSWORD = $null; $env:EQ2_WORLD_ACCOUNT = $null; $env:EQ2_WORLD_PASSWORD = $null; try { $out = Join-Path '${CMAKE_BINARY_DIR}' ('client_acceptance_failure_' + [guid]::NewGuid().ToString('N')); $output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_run_real_client_acceptance_session.ps1' -BuildDir '${CMAKE_BINARY_DIR}' -LoginConfigPath '${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example' -WorldConfigPath '${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example' -OutputDir $out -RunForMs 3000 *>&1; $exitCode = $LASTEXITCODE; $report = Get-ChildItem -Path $out -Recurse -Filter session.md -ErrorAction SilentlyContinue | Select-Object -First 1; if ($exitCode -eq 0 -or $null -eq $report) { $output | Write-Host; exit 1 }; $text = Get-Content -LiteralPath $report.FullName -Raw; if ($text -notmatch 'Status: failed before ready' -or $text -notmatch 'is required unless -DryRun') { $output | Write-Host; Write-Host $text; exit 1 } } finally { $env:EQ2_DB_PASSWORD = $oldDb; $env:EQ2_WORLD_ACCOUNT = $oldWorldAccount; $env:EQ2_WORLD_PASSWORD = $oldWorldPassword }"
      )

      add_test(NAME source2_set_eq2_client_login_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('client_login_config_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $cfg = Join-Path $dir 'eq2_default.ini'; Set-Content -LiteralPath $cfg -Value @('cl_ls_address 192.168.1.243', 'cl_screenwidth 1024'); $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_set_eq2_client_login.ps1' -ConfigPath $cfg -LoginHost 192.168.1.41 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); $after = Get-Content -LiteralPath $cfg -Raw; if ($text -notmatch 'Would change' -or $text -notmatch 'cl_ls_address 192.168.1.41' -or $after -notmatch 'cl_ls_address 192.168.1.243') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_set_eq2_client_login_backup_restore
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('client_login_restore_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $cfg = Join-Path $dir 'eq2_default.ini'; Set-Content -LiteralPath $cfg -Value @('cl_ls_address 192.168.1.243', 'cl_screenwidth 1024'); $output = @(); $output += & '${CMAKE_SOURCE_DIR}/scripts/source2_set_eq2_client_login.ps1' -ConfigPath $cfg -LoginHost 192.168.1.41 *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $changed = Get-Content -LiteralPath $cfg -Raw; if ($changed -notmatch 'cl_ls_address 192.168.1.41' -or !(Test-Path -LiteralPath ($cfg + '.source2-backup'))) { $output | Write-Host; exit 1 }; $output += & '${CMAKE_SOURCE_DIR}/scripts/source2_set_eq2_client_login.ps1' -ConfigPath $cfg -RestoreOriginal *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $restored = Get-Content -LiteralPath $cfg -Raw; if ($restored -notmatch 'cl_ls_address 192.168.1.243') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_prepare_eq2_client_sandbox_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$source = Join-Path '${CMAKE_BINARY_DIR}' ('client_source_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $source | Out-Null; Set-Content -LiteralPath (Join-Path $source 'eq2_default.ini') -Value 'cl_ls_address 192.168.1.243'; New-Item -ItemType File -Path (Join-Path $source 'EverQuest2.exe') | Out-Null; New-Item -ItemType Directory -Force -Path (Join-Path $source 'paks') | Out-Null; New-Item -ItemType Directory -Force -Path (Join-Path $source 'UI') | Out-Null; $out = Join-Path '${CMAKE_BINARY_DIR}' ('client_sandbox_' + [guid]::NewGuid().ToString('N')); $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_prepare_eq2_client_sandbox.ps1' -SourceDir $source -OutputDir $out -LoginHost 192.168.1.41 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'EQ2 client sandbox dry run' -or $text -notmatch 'copy EverQuest2.exe' -or $text -notmatch 'junction paks' -or $text -notmatch 'copy UI' -or $text -notmatch 'cl_ls_address 192.168.1.41') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_prepare_eq2_client_sandbox_creates_copy_and_junction
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$source = Join-Path '${CMAKE_BINARY_DIR}' ('client_source_create_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $source | Out-Null; Set-Content -LiteralPath (Join-Path $source 'eq2_default.ini') -Value @('cl_ls_address 192.168.1.243','cl_screenwidth 1024'); Set-Content -LiteralPath (Join-Path $source 'EverQuest2.exe') -Value 'exe'; $paks = Join-Path $source 'paks'; $ui = Join-Path $source 'UI'; New-Item -ItemType Directory -Force -Path $paks | Out-Null; Set-Content -LiteralPath (Join-Path $paks 'asset.vpk') -Value 'asset'; New-Item -ItemType Directory -Force -Path $ui | Out-Null; Set-Content -LiteralPath (Join-Path $ui 'ui.xml') -Value 'ui'; $out = Join-Path '${CMAKE_BINARY_DIR}' ('client_sandbox_create_' + [guid]::NewGuid().ToString('N')); $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_prepare_eq2_client_sandbox.ps1' -SourceDir $source -OutputDir $out -LoginHost 192.168.1.41 *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $cfg = Get-Content -LiteralPath (Join-Path $out 'eq2_default.ini') -Raw; $backup = Test-Path -LiteralPath (Join-Path $out 'eq2_default.ini.source2-backup'); $uiCopy = Test-Path -LiteralPath (Join-Path (Join-Path $out 'UI') 'ui.xml'); $pakItem = Get-Item -LiteralPath (Join-Path $out 'paks'); $manifest = Get-Content -LiteralPath (Join-Path $out 'source2_sandbox_manifest.tsv') -Raw; if ($cfg -notmatch 'cl_ls_address 192.168.1.41' -or !$backup -or !$uiCopy -or !(($pakItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq [System.IO.FileAttributes]::ReparsePoint) -or $manifest -notmatch 'LoginHost\\t192.168.1.41' -or $manifest -notmatch 'junction\\tpaks' -or $manifest -notmatch 'directory\\tUI') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_real_client_acceptance_session_passes_sample
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('acceptance_audit_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $login = Join-Path $dir 'login.stdout.log'; $loginErr = Join-Path $dir 'login.stderr.log'; $world = Join-Path $dir 'world.stdout.log'; $worldErr = Join-Path $dir 'world.stderr.log'; Set-Content -LiteralPath $login -Value @('event=world_registered registered_worlds=1','event=login_accepted login_attempts=1','event=login_rejected login_failures=1'); Set-Content -LiteralPath $loginErr -Value ''; Set-Content -LiteralPath $world -Value 'world ready'; Set-Content -LiteralPath $worldErr -Value ''; $clientLogs = Join-Path $dir 'client-logs'; New-Item -ItemType Directory -Force -Path $clientLogs | Out-Null; Set-Content -LiteralPath (Join-Path $clientLogs 'eq2_packet_log.jsonl') -Value '{}'; Set-Content -LiteralPath (Join-Path $clientLogs 'manifest.tsv') -Value @('Source`tExists`tLength`tLastWriteTime`tSnapshot','eq2_packet_log.jsonl`tTrue`t2`t2026-05-15T00:00:00Z`tcopy'); $bt = [char]96; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','## Logs',(\"- Login stdout: {0}{0}{1}{0}{0}\" -f $bt,$login),(\"- Login stderr: {0}{0}{1}{0}{0}\" -f $bt,$loginErr),(\"- World stdout: {0}{0}{1}{0}{0}\" -f $bt,$world),(\"- World stderr: {0}{0}{1}{0}{0}\" -f $bt,$worldErr),(\"- Client log snapshots: {0}{0}{1}{0}{0}\" -f $bt,$clientLogs),'','## Manual Results','','| Gate | Result | Notes |','| --- | --- | --- |','| Login progresses past Trying login server #1 | Passed | |','| World list shows Source2 World | Passed | |','| Character list appears for the test account | Passed | |','| Play/select reaches world handoff | Passed | |','| Create/delete test character works | Passed | |','| Wrong-password attempt logs login_rejected without a password value | Passed | |'); $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_audit_real_client_acceptance_session.ps1' -SessionDir $dir -RequireManualPass -RequireLoginAccepted -RequireClientLogSnapshot -RequireWorldRegistered -RequireFailedLoginDiagnostic -ForbiddenSecret 'wrongpass' *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'acceptance evidence audit passed') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_real_client_acceptance_session_rejects_missing_login_accepted
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('acceptance_audit_missing_login_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $login = Join-Path $dir 'login.stdout.log'; $empty = Join-Path $dir 'empty.log'; Set-Content -LiteralPath $login -Value 'event=world_registered registered_worlds=1'; Set-Content -LiteralPath $empty -Value ''; $bt = [char]96; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','## Logs',(\"- Login stdout: {0}{0}{1}{0}{0}\" -f $bt,$login),(\"- Login stderr: {0}{0}{1}{0}{0}\" -f $bt,$empty),(\"- World stdout: {0}{0}{1}{0}{0}\" -f $bt,$empty),(\"- World stderr: {0}{0}{1}{0}{0}\" -f $bt,$empty),(\"- Client log snapshots: {0}{0}not requested{0}{0}\" -f $bt),'','## Manual Results','','| Gate | Result | Notes |','| --- | --- | --- |','| Login progresses past Trying login server #1 | Passed | |','| World list shows Source2 World | Passed | |','| Character list appears for the test account | Passed | |','| Play/select reaches world handoff | Passed | |','| Create/delete test character works | Passed | |'); $output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_audit_real_client_acceptance_session.ps1' -SessionDir $dir -RequireManualPass -RequireLoginAccepted -RequireWorldRegistered *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'login_accepted') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_real_client_acceptance_session_rejects_pending_gate
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('acceptance_audit_pending_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $log = Join-Path $dir 'login.stdout.log'; Set-Content -LiteralPath $log -Value 'event=world_registered registered_worlds=1'; $empty = Join-Path $dir 'empty.log'; Set-Content -LiteralPath $empty -Value ''; $bt = [char]96; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','## Logs',(\"- Login stdout: {0}{0}{1}{0}{0}\" -f $bt,$log),(\"- Login stderr: {0}{0}{1}{0}{0}\" -f $bt,$empty),(\"- World stdout: {0}{0}{1}{0}{0}\" -f $bt,$empty),(\"- World stderr: {0}{0}{1}{0}{0}\" -f $bt,$empty),(\"- Client log snapshots: {0}{0}not requested{0}{0}\" -f $bt),'','## Manual Results','','| Gate | Result | Notes |','| --- | --- | --- |','| Login progresses past Trying login server #1 | Pending | |'); $output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_audit_real_client_acceptance_session.ps1' -SessionDir $dir -RequireManualPass *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'not passed') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_real_client_acceptance_session_rejects_forbidden_client_snapshot_secret
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('acceptance_audit_client_secret_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $login = Join-Path $dir 'login.stdout.log'; $loginErr = Join-Path $dir 'login.stderr.log'; $world = Join-Path $dir 'world.stdout.log'; $worldErr = Join-Path $dir 'world.stderr.log'; Set-Content -LiteralPath $login -Value @('event=world_registered registered_worlds=1','event=login_rejected login_failures=1'); Set-Content -LiteralPath $loginErr -Value ''; Set-Content -LiteralPath $world -Value 'world ready'; Set-Content -LiteralPath $worldErr -Value ''; $clientLogs = Join-Path $dir 'client-logs'; New-Item -ItemType Directory -Force -Path $clientLogs | Out-Null; Set-Content -LiteralPath (Join-Path $clientLogs 'eq2_packet_log.jsonl') -Value 'client leak wrongpass'; Set-Content -LiteralPath (Join-Path $clientLogs 'manifest.tsv') -Value @('Source`tExists`tLength`tLastWriteTime`tSnapshot','eq2_packet_log.jsonl`tTrue`t20`t2026-05-15T00:00:00Z`tcopy'); $bt = [char]96; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','## Logs',(\"- Login stdout: {0}{0}{1}{0}{0}\" -f $bt,$login),(\"- Login stderr: {0}{0}{1}{0}{0}\" -f $bt,$loginErr),(\"- World stdout: {0}{0}{1}{0}{0}\" -f $bt,$world),(\"- World stderr: {0}{0}{1}{0}{0}\" -f $bt,$worldErr),(\"- Client log snapshots: {0}{0}{1}{0}{0}\" -f $bt,$clientLogs)); $output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_audit_real_client_acceptance_session.ps1' -SessionDir $dir -RequireClientLogSnapshot -ForbiddenSecret wrongpass *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'Client log snapshots contain the forbidden secret value') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_diagnose_git_permissions_writable_temp_repo
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$repo = Join-Path '${CMAKE_BINARY_DIR}' ('git_permission_diag_' + [guid]::NewGuid().ToString('N')); $git = Join-Path $repo '.git'; New-Item -ItemType Directory -Force -Path $git | Out-Null; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_diagnose_git_permissions.ps1' -RepoRoot $repo -WriteAclReport -NoRecommendations *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); $report = Get-ChildItem -Path (Join-Path $repo 'artifacts/migrationv3-git-acl') -Filter '*-git-acl-report.txt' -ErrorAction SilentlyContinue | Select-Object -First 1; if ($text -notmatch 'Git metadata write probe: passed' -or $null -eq $report) { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_capture_login_packets_dry_run
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_capture_login_packets.ps1' -OutputDir '${CMAKE_BINARY_DIR}/capture dry run' -CaptureName 'source2 capture test' -TargetHost 127.0.0.1 -DurationSeconds 5 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Source2 login packet capture dry run' -or $text -notmatch 'pktmon filter add source2-login-9100' -or $text -notmatch '-t UDP' -or $text -notmatch '-p 9100' -or $text -notmatch '-i 127.0.0.1' -or $text -notmatch 'pktmon start --capture' -or $text -notmatch 'pktmon etl2pcap' -or $text -notmatch 'capture.md') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_commit_migrationv3_phases_dry_run_defaults_to_phase_8
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_commit_migrationv3_phases.ps1' -RepoRoot '${CMAKE_SOURCE_DIR}' -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Phase range: 0\\.\\.8' -or $text -notmatch 'Document login parity packet baseline' -or $text -notmatch 'Add login parity diagnostics' -or $text -match 'Accept source2 login server parity') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_commit_migrationv3_phases_rejects_phase_9_without_flag
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_commit_migrationv3_phases.ps1' -RepoRoot '${CMAKE_SOURCE_DIR}' -ThroughPhase 9 -DryRun *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'Phase 9 requires -IncludePhase9') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_commit_migrationv3_phases_includes_phase_9_with_flag
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_commit_migrationv3_phases.ps1' -RepoRoot '${CMAKE_SOURCE_DIR}' -IncludePhase9 -DryRun *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Phase range: 0\\.\\.9' -or $text -notmatch 'Accept source2 login server parity' -or $text -notmatch 'source2_commit_migrationv3_phases.ps1') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_commit_migrationv3_phases_commits_temp_repo
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$repo = Join-Path '${CMAKE_BINARY_DIR}' ('phase_commit_repo_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $repo | Out-Null; & git -C $repo init | Out-Null; & git -C $repo config user.email source2-test@example.invalid; & git -C $repo config user.name 'Source2 Test'; Set-Content -LiteralPath (Join-Path $repo '.seed') -Value 'seed'; & git -C $repo add .seed; & git -C $repo commit -m seed | Out-Null; foreach ($path in @('migrationv3/README.md','migrationv3/phase_0_baseline_packet_audit.md','migrationv3/packet_baseline.md','migrationv3/phase_1_legacy_login_inventory.md','migrationv3/legacy_scenario_inventory.md')) { $full = Join-Path $repo $path; New-Item -ItemType Directory -Force -Path (Split-Path -Parent $full) | Out-Null; Set-Content -LiteralPath $full -Value $path }; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_commit_migrationv3_phases.ps1' -RepoRoot $repo -ThroughPhase 1 -SkipGitWritableCheck *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $subjects = @(& git -C $repo log --format=%s -n 3); if ($subjects -notcontains 'Document login parity packet baseline' -or $subjects -notcontains 'Inventory legacy login server parity scope') { $output | Write-Host; $subjects | Write-Host; exit 1 }"
      )

      add_test(NAME source2_record_real_client_acceptance_results_accepts_all
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('acceptance_record_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','## Manual Results','','| Gate | Result | Notes |','| --- | --- | --- |','| Login progresses past Trying login server #1 | Pending | |','| World list shows Source2 World | Pending | |','| Character list appears for the test account | Pending | |','| Play/select reaches world handoff | Pending | |','| Create/delete test character works, if exercised | Pending | |','| Wrong-password attempt logs login_rejected without a password value | Pending | |'); $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_record_real_client_acceptance_results.ps1' -SessionDir $dir -AcceptAll -RequireAllRecorded -LoginNotes 'local client passed' *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = Get-Content -LiteralPath (Join-Path $dir 'session.md') -Raw; if ($text -match 'Pending' -or $text -notmatch 'local client passed' -or $text -notmatch '\\| World list shows Source2 World \\| Passed \\|') { $output | Write-Host; Write-Host $text; exit 1 }"
      )

      add_test(NAME source2_record_real_client_acceptance_results_rejects_pending
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$dir = Join-Path '${CMAKE_BINARY_DIR}' ('acceptance_record_pending_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $dir | Out-Null; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','## Manual Results','','| Gate | Result | Notes |','| --- | --- | --- |','| Login progresses past Trying login server #1 | Pending | |','| World list shows Source2 World | Pending | |','| Character list appears for the test account | Pending | |','| Play/select reaches world handoff | Pending | |','| Create/delete test character works, if exercised | Pending | |','| Wrong-password attempt logs login_rejected without a password value | Pending | |'); $output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_record_real_client_acceptance_results.ps1' -SessionDir $dir -RequireAllRecorded *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'incomplete') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_migrationv3_completion_allows_known_blockers
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${CMAKE_SOURCE_DIR}/scripts/source2_audit_migrationv3_completion.ps1' -RepoRoot '${CMAKE_SOURCE_DIR}' -RequireNoBlockedStatus -RequirePhaseCommits -RequireRealClientEvidence -AllowKnownBlockers *>&1; if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) { $output | Write-Host; exit 1 }; $text = [string]::Join([Environment]::NewLine, $output); if ($text -notmatch 'Known blockers accepted') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_migrationv3_completion_rejects_current_blockers
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_audit_migrationv3_completion.ps1' -RepoRoot '${CMAKE_SOURCE_DIR}' -RequireNoBlockedStatus -RequirePhaseCommits -RequireRealClientEvidence *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'migrationv3 is not complete') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_migrationv3_completion_rejects_missing_phase_git_add
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$tmpRoot = Join-Path '${CMAKE_BINARY_DIR}' ('migrationv3_audit_missing_git_add_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $tmpRoot | Out-Null; Copy-Item -Recurse -Path '${CMAKE_SOURCE_DIR}/migrationv3' -Destination $tmpRoot; $phase = Join-Path $tmpRoot 'migrationv3/phase_0_baseline_packet_audit.md'; $lines = Get-Content -LiteralPath $phase; $lines = $lines | Where-Object { $_ -notmatch '^git add\\b' }; Set-Content -LiteralPath $phase -Value $lines; $output = & '${EQ2_POWERSHELL_EXE}' -NoProfile -ExecutionPolicy Bypass -File '${CMAKE_SOURCE_DIR}/scripts/source2_audit_migrationv3_completion.ps1' -RepoRoot '${CMAKE_SOURCE_DIR}' -MigrationDir (Join-Path $tmpRoot 'migrationv3') *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'Phase 0 file does not include a git add command') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_migrationv3_completion_rejects_missing_lan_client_evidence
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$root = Join-Path '${CMAKE_BINARY_DIR}' ('migrationv3_audit_missing_lan_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $root | Out-Null; $dirs = @(); foreach ($name in @('local_a','local_b')) { $dir = Join-Path $root $name; New-Item -ItemType Directory -Force -Path $dir | Out-Null; $login = Join-Path $dir 'login.stdout.log'; $loginErr = Join-Path $dir 'login.stderr.log'; $world = Join-Path $dir 'world.stdout.log'; $worldErr = Join-Path $dir 'world.stderr.log'; Set-Content -LiteralPath $login -Value @('event=world_registered registered_worlds=1','event=login_accepted login_attempts=1','event=login_rejected login_failures=1'); Set-Content -LiteralPath $loginErr -Value ''; Set-Content -LiteralPath $world -Value 'world ready'; Set-Content -LiteralPath $worldErr -Value ''; $clientLogs = Join-Path $dir 'client-logs'; New-Item -ItemType Directory -Force -Path $clientLogs | Out-Null; Set-Content -LiteralPath (Join-Path $clientLogs 'eq2_packet_log.jsonl') -Value '{}'; Set-Content -LiteralPath (Join-Path $clientLogs 'manifest.tsv') -Value @('Source`tExists`tLength`tLastWriteTime`tSnapshot','eq2_packet_log.jsonl`tTrue`t2`t2026-05-15T00:00:00Z`tcopy'); $bt = [char]96; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','Started: test','Status: ready','','## Runtime','','- Client login setting: `cl_ls_address 127.0.0.1`','','## Logs',(\"- Login stdout: {0}{0}{1}{0}{0}\" -f $bt,$login),(\"- Login stderr: {0}{0}{1}{0}{0}\" -f $bt,$loginErr),(\"- World stdout: {0}{0}{1}{0}{0}\" -f $bt,$world),(\"- World stderr: {0}{0}{1}{0}{0}\" -f $bt,$worldErr),(\"- Client log snapshots: {0}{0}{1}{0}{0}\" -f $bt,$clientLogs),'','## Manual Results','','| Gate | Result | Notes |','| --- | --- | --- |','| Login progresses past Trying login server #1 | Passed | |','| World list shows Source2 World | Passed | |','| Character list appears for the test account | Passed | |','| Play/select reaches world handoff | Passed | |','| Create/delete test character works | Passed | |','| Wrong-password attempt logs login_rejected without a password value | Passed | |'); $dirs += $dir }; $params = @{ RepoRoot = '${CMAKE_SOURCE_DIR}'; SessionDirs = $dirs; RequiredClientTargetHosts = @('127.0.0.1','192.168.1.41') }; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_audit_migrationv3_completion.ps1' @params -RequireRealClientEvidence *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'Missing real-client evidence for required client target host: 192.168.1.41') { $output | Write-Host; exit 1 }"
      )

      add_test(NAME source2_audit_migrationv3_completion_rejects_forbidden_client_snapshot_secret
        COMMAND ${EQ2_POWERSHELL_EXE} -NoProfile -ExecutionPolicy Bypass
                -Command "$root = Join-Path '${CMAKE_BINARY_DIR}' ('migrationv3_audit_secret_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force -Path $root | Out-Null; $dirs = @(); foreach ($targetHost in @('127.0.0.1','192.168.1.41')) { $dir = Join-Path $root ($targetHost -replace '[^A-Za-z0-9]','_'); New-Item -ItemType Directory -Force -Path $dir | Out-Null; $login = Join-Path $dir 'login.stdout.log'; $loginErr = Join-Path $dir 'login.stderr.log'; $world = Join-Path $dir 'world.stdout.log'; $worldErr = Join-Path $dir 'world.stderr.log'; Set-Content -LiteralPath $login -Value @('event=world_registered registered_worlds=1','event=login_accepted login_attempts=1','event=login_rejected login_failures=1'); Set-Content -LiteralPath $loginErr -Value ''; Set-Content -LiteralPath $world -Value 'world ready'; Set-Content -LiteralPath $worldErr -Value ''; $clientLogs = Join-Path $dir 'client-logs'; New-Item -ItemType Directory -Force -Path $clientLogs | Out-Null; $clientValue = '{}'; if ($targetHost -eq '192.168.1.41') { $clientValue = 'client leaked wrongpass' }; Set-Content -LiteralPath (Join-Path $clientLogs 'eq2_packet_log.jsonl') -Value $clientValue; Set-Content -LiteralPath (Join-Path $clientLogs 'manifest.tsv') -Value @('Source`tExists`tLength`tLastWriteTime`tSnapshot','eq2_packet_log.jsonl`tTrue`t20`t2026-05-15T00:00:00Z`tcopy'); $bt = [char]96; Set-Content -LiteralPath (Join-Path $dir 'session.md') -Value @('# Source2 Real-Client Acceptance Session','','Started: test','Status: ready','','## Runtime','',(\"- Client login setting: {0}cl_ls_address {1}{0}\" -f $bt,$targetHost),'','## Logs',(\"- Login stdout: {0}{0}{1}{0}{0}\" -f $bt,$login),(\"- Login stderr: {0}{0}{1}{0}{0}\" -f $bt,$loginErr),(\"- World stdout: {0}{0}{1}{0}{0}\" -f $bt,$world),(\"- World stderr: {0}{0}{1}{0}{0}\" -f $bt,$worldErr),(\"- Client log snapshots: {0}{0}{1}{0}{0}\" -f $bt,$clientLogs),'','## Manual Results','','| Gate | Result | Notes |','| --- | --- | --- |','| Login progresses past Trying login server #1 | Passed | |','| World list shows Source2 World | Passed | |','| Character list appears for the test account | Passed | |','| Play/select reaches world handoff | Passed | |','| Create/delete test character works | Passed | |','| Wrong-password attempt logs login_rejected without a password value | Passed | |'); $dirs += $dir }; $params = @{ RepoRoot = '${CMAKE_SOURCE_DIR}'; SessionDirs = $dirs; RequiredClientTargetHosts = @('127.0.0.1','192.168.1.41'); ForbiddenSecret = 'wrongpass' }; $output = & '${CMAKE_SOURCE_DIR}/scripts/source2_audit_migrationv3_completion.ps1' @params -RequireRealClientEvidence *>&1; $exitCode = $LASTEXITCODE; $text = [string]::Join([Environment]::NewLine, $output); if ($exitCode -eq 0 -or $text -notmatch 'Client log snapshots contain the forbidden secret value') { $output | Write-Host; exit 1 }"
      )
    endif()
  endif()

  add_test(NAME eq2_login_server_validate_config
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
  )

  add_test(NAME eq2_login_server_help_lists_config_precedence
    COMMAND eq2_login_server --help
  )
  set_tests_properties(eq2_login_server_help_lists_config_precedence
    PROPERTIES PASS_REGULAR_EXPRESSION "Precedence: INI config, environment variables, then CLI overrides"
  )

  add_test(NAME eq2_login_server_help_lists_skip_account_auth
    COMMAND eq2_login_server --help
  )
  set_tests_properties(eq2_login_server_help_lists_skip_account_auth
    PROPERTIES PASS_REGULAR_EXPRESSION "--skip-account-auth"
  )

  add_test(NAME eq2_login_server_help_lists_db_tls
    COMMAND eq2_login_server --help
  )
  set_tests_properties(eq2_login_server_help_lists_db_tls
    PROPERTIES PASS_REGULAR_EXPRESSION "--db-tls"
  )

  add_test(NAME eq2_login_server_example_config_parses
    COMMAND eq2_login_server --validate-config --config "${CMAKE_SOURCE_DIR}/source2/config/login_server.ini.example"
  )

  add_test(NAME eq2_login_server_target_config_parses
    COMMAND eq2_login_server --validate-config --config "${CMAKE_SOURCE_DIR}/source2/config/login_server.eq2emu-target.ini.example"
  )

  add_test(NAME eq2_login_server_env_overrides_config
    COMMAND eq2_login_server --validate-config --config "${CMAKE_SOURCE_DIR}/source2/config/login_server.env-override-test.ini.example"
  )
  set_tests_properties(eq2_login_server_env_overrides_config
    PROPERTIES ENVIRONMENT "EQ2_LOGIN_PORT=9100"
  )

  add_test(NAME eq2_login_server_configured_one_byte_opcodes_parse
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --login-opcode-width 1 --login-request-opcode 0x01
            --login-reply-opcode 0x02 --login-world-list-opcode 0x03
            --login-all-worlds-request-opcode 0x04 --login-characters-request-opcode 0x05
            --login-characters-reply-opcode 0x06
            --login-create-character-request-opcode 0x07
            --login-create-character-reply-opcode 0x08
            --login-delete-character-request-opcode 0x09
            --login-delete-character-reply-opcode 0x0a
            --login-play-character-request-opcode 0x0b
            --login-play-character-reply-opcode 0x0c
            --login-client-crashlog-reply-opcode 0x0d
            --login-client-eq2-crashlog-reply-opcode 0x0e
            --login-client-verifylog-reply-opcode 0x0f
            --login-client-alertlog-reply-opcode 0x10
            --login-client-baselog-reply-opcode 0x11
  )

  add_test(NAME eq2_login_server_database_opcode_source_parses
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --login-opcode-source database --login-opcode-width packed
  )

  add_test(NAME eq2_login_server_configured_packed_opcodes_parse
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --login-opcode-source config --login-opcode-width packed
            --login-request-opcode 0x00 --login-reply-opcode 0x04
            --login-world-list-opcode 0x08 --login-all-worlds-request-opcode 0x07
            --login-characters-request-opcode 0x09 --login-characters-reply-opcode 0x0a
            --login-create-character-request-opcode 0x0b
            --login-create-character-reply-opcode 0x0c
            --login-delete-character-request-opcode 0x10
            --login-delete-character-reply-opcode 0x11
            --login-play-character-request-opcode 0x12
            --login-play-character-reply-opcode 0x13
            --login-client-crashlog-reply-opcode 0x0100
            --login-client-eq2-crashlog-reply-opcode 0x0101
            --login-client-verifylog-reply-opcode 0x0102
            --login-client-alertlog-reply-opcode 0x0103
            --login-client-baselog-reply-opcode 0x0104
  )

  add_test(NAME eq2_login_server_db_tls_cli_parses
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --db-name eq2ls --db-user eq2emu --db-tls false
  )

  add_test(NAME eq2_login_server_rejects_invalid_db_tls
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --db-name eq2ls --db-user eq2emu --set db.tls=maybe
  )
  set_tests_properties(eq2_login_server_rejects_invalid_db_tls
    PROPERTIES WILL_FAIL TRUE PASS_REGULAR_EXPRESSION "MariaDB TLS setting must be a boolean"
  )

  add_test(NAME eq2_login_server_one_byte_opcode_rejects_large_value
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --login-opcode-width 1 --login-request-opcode 0x1234
            --login-reply-opcode 0x02 --login-world-list-opcode 0x03
            --login-all-worlds-request-opcode 0x04 --login-characters-request-opcode 0x05
            --login-characters-reply-opcode 0x06
            --login-create-character-request-opcode 0x07
            --login-create-character-reply-opcode 0x08
            --login-delete-character-request-opcode 0x09
            --login-delete-character-reply-opcode 0x0a
            --login-play-character-request-opcode 0x0b
            --login-play-character-reply-opcode 0x0c
            --login-client-crashlog-reply-opcode 0x0d
            --login-client-eq2-crashlog-reply-opcode 0x0e
            --login-client-verifylog-reply-opcode 0x0f
            --login-client-alertlog-reply-opcode 0x10
            --login-client-baselog-reply-opcode 0x11
  )
  set_tests_properties(eq2_login_server_one_byte_opcode_rejects_large_value
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_login_server_strict_config_rejects_invalid_port
    COMMAND eq2_login_server --strict-config
  )
  set_tests_properties(eq2_login_server_strict_config_rejects_invalid_port
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_login_server_rejects_invalid_client_version_config
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --set login.client_version=invalid
  )
  set_tests_properties(eq2_login_server_rejects_invalid_client_version_config
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_login_server_rejects_invalid_expected_world_count
    COMMAND eq2_login_server --smoke-login-live --expect-world-count invalid
  )
  set_tests_properties(eq2_login_server_rejects_invalid_expected_world_count
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_login_server_smoke_rejects_missing_expected_world
    COMMAND eq2_login_server --smoke-login-live --expect-world-count 1
  )
  set_tests_properties(eq2_login_server_smoke_rejects_missing_expected_world
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_login_server_smoke_uses_env_credentials
    COMMAND eq2_login_server --smoke-login-live
  )
  set_tests_properties(eq2_login_server_smoke_uses_env_credentials
    PROPERTIES ENVIRONMENT "EQ2_LOGIN_USERNAME=envtester;EQ2_LOGIN_PASSWORD=envpass"
  )

  add_test(NAME eq2_login_server_serve_requires_db_config
    COMMAND eq2_login_server --serve --run-for-ms 1 --login-address 127.0.0.1 --login-port 0
  )
  set_tests_properties(eq2_login_server_serve_requires_db_config
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_login_server_serve_rejects_invalid_db_port
    COMMAND eq2_login_server --serve --run-for-ms 1 --db-host 127.0.0.1 --db-port invalid --db-name eq2ls --db-user eq2emu
  )
  set_tests_properties(eq2_login_server_serve_rejects_invalid_db_port
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_login_server_check_login_db_requires_db_config
    COMMAND eq2_login_server --check-login-db
  )
  set_tests_properties(eq2_login_server_check_login_db_requires_db_config
    PROPERTIES
      WILL_FAIL TRUE
      ENVIRONMENT "EQ2_LOGIN_USERNAME=tester;EQ2_LOGIN_PASSWORD=correct"
  )

  add_test(NAME eq2_login_server_check_login_db_rejects_invalid_db_port
    COMMAND eq2_login_server --check-login-db --db-host 127.0.0.1 --db-port invalid --db-name eq2ls --db-user eq2emu
  )
  set_tests_properties(eq2_login_server_check_login_db_rejects_invalid_db_port
    PROPERTIES
      WILL_FAIL TRUE
      ENVIRONMENT "EQ2_LOGIN_USERNAME=tester;EQ2_LOGIN_PASSWORD=correct"
  )

  add_test(NAME eq2_login_server_check_login_db_skip_account_auth_rejects_invalid_db_port
    COMMAND eq2_login_server --check-login-db --skip-account-auth --db-host 127.0.0.1 --db-port invalid --db-name eq2ls --db-user eq2emu
  )
  set_tests_properties(eq2_login_server_check_login_db_skip_account_auth_rejects_invalid_db_port
    PROPERTIES
      WILL_FAIL TRUE
      PASS_REGULAR_EXPRESSION "db.port must be a valid TCP port"
  )

  add_test(NAME eq2_login_server_probe_database_opcode_source_requires_db_config
    COMMAND eq2_login_server --probe-login --login-opcode-source database
            --connect-host 127.0.0.1 --connect-port 9
  )
  set_tests_properties(eq2_login_server_probe_database_opcode_source_requires_db_config
    PROPERTIES
      WILL_FAIL TRUE
      ENVIRONMENT "EQ2_LOGIN_USERNAME=tester;EQ2_LOGIN_PASSWORD=correct"
  )

  add_test(NAME eq2_world_server_validate_config
    COMMAND eq2_world_server --validate-config --world-address 127.0.0.1 --world-port 0
  )

  add_test(NAME eq2_world_server_help_lists_environment_aliases
    COMMAND eq2_world_server --help
  )
  set_tests_properties(eq2_world_server_help_lists_environment_aliases
    PROPERTIES PASS_REGULAR_EXPRESSION "EQ2_WORLD_ACCOUNT"
  )

  add_test(NAME eq2_world_server_help_lists_db_tls
    COMMAND eq2_world_server --help
  )
  set_tests_properties(eq2_world_server_help_lists_db_tls
    PROPERTIES PASS_REGULAR_EXPRESSION "--db-tls"
  )

  add_test(NAME eq2_world_server_example_config_parses
    COMMAND eq2_world_server --validate-config --config "${CMAKE_SOURCE_DIR}/source2/config/world_server.ini.example"
  )

  add_test(NAME eq2_world_server_target_config_parses
    COMMAND eq2_world_server --validate-config --config "${CMAKE_SOURCE_DIR}/source2/config/world_server.eq2emu-target.ini.example"
  )

  add_test(NAME eq2_world_server_env_overrides_config
    COMMAND eq2_world_server --validate-config --config "${CMAKE_SOURCE_DIR}/source2/config/world_server.env-override-test.ini.example"
  )
  set_tests_properties(eq2_world_server_env_overrides_config
    PROPERTIES ENVIRONMENT "EQ2_WORLD_PORT=9101;EQ2_LOGIN_PORT=9100;EQ2_WORLD_OPCODE_WIDTH=packed"
  )

  add_test(NAME eq2_world_server_db_tls_cli_parses
    COMMAND eq2_world_server --validate-config --world-port 0 --login-port 0
            --db-name eq2emu --db-user eq2emu --db-tls false
  )

  add_test(NAME eq2_world_server_rejects_invalid_db_tls
    COMMAND eq2_world_server --validate-config --world-port 0 --login-port 0
            --db-name eq2emu --db-user eq2emu --set world_db.tls=maybe
  )
  set_tests_properties(eq2_world_server_rejects_invalid_db_tls
    PROPERTIES WILL_FAIL TRUE PASS_REGULAR_EXPRESSION "MariaDB world DB TLS setting must be a boolean"
  )

  add_test(NAME eq2_world_server_rejects_invalid_opcode_width
    COMMAND eq2_world_server --validate-config --world-opcode-width invalid
  )
  set_tests_properties(eq2_world_server_rejects_invalid_opcode_width
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_world_server_strict_config_rejects_invalid_port
    COMMAND eq2_world_server --strict-config
  )
  set_tests_properties(eq2_world_server_strict_config_rejects_invalid_port
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_world_server_serve_requires_registration_config
    COMMAND eq2_world_server --serve --run-for-ms 1
            --world-address 127.0.0.1 --world-port 0
            --db-host 127.0.0.1 --db-name eq2emu --db-user eq2emu
  )
  set_tests_properties(eq2_world_server_serve_requires_registration_config
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_world_server_serve_rejects_invalid_db_port
    COMMAND eq2_world_server --serve --run-for-ms 1
            --world-address 127.0.0.1 --world-port 0
            --world-name Test
            --db-host 127.0.0.1 --db-port invalid --db-name eq2emu --db-user eq2emu
  )
  set_tests_properties(eq2_world_server_serve_rejects_invalid_db_port
    PROPERTIES
      WILL_FAIL TRUE
      ENVIRONMENT "EQ2_WORLD_ACCOUNT=world-account;EQ2_WORLD_PASSWORD=secret"
  )

  add_test(NAME eq2_world_server_check_world_db_requires_db_config
    COMMAND eq2_world_server --check-world-db
  )
  set_tests_properties(eq2_world_server_check_world_db_requires_db_config
    PROPERTIES WILL_FAIL TRUE
  )

  add_test(NAME eq2_world_server_check_world_db_rejects_invalid_db_port
    COMMAND eq2_world_server --check-world-db --db-host 127.0.0.1
            --db-port invalid --db-name eq2emu --db-user eq2emu
  )
  set_tests_properties(eq2_world_server_check_world_db_rejects_invalid_db_port
    PROPERTIES WILL_FAIL TRUE
  )

  if(EQ2_SOURCE2_ENABLE_LIVE_SMOKE_TESTS)
    add_test(NAME eq2_login_live_smoke
      COMMAND eq2_login_server --smoke-login-live
    )

    add_test(NAME eq2_world_live_smoke
      COMMAND eq2_world_server --smoke-world-live
    )
  endif()
endif()
