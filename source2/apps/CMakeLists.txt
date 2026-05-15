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
      foreach(script_name IN ITEMS source2_check_mariadb_tcp.ps1 source2_live_login_verify.ps1 source2_owner_live_login_preflight.ps1 source2_target_live_login_ctest.ps1 source2_run_target_live_login_gate.ps1 source2_start_login_server.ps1 source2_probe_login_server.ps1 source2_start_world_server.ps1 source2_configure_login_firewall.ps1 source2_prepare_deploy_config.ps1)
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
  )

  add_test(NAME eq2_login_server_database_opcode_source_parses
    COMMAND eq2_login_server --validate-config --login-address 127.0.0.1 --login-port 0
            --login-opcode-source database --login-opcode-width 1
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
    PROPERTIES ENVIRONMENT "EQ2_WORLD_PORT=9101;EQ2_LOGIN_PORT=9100"
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
