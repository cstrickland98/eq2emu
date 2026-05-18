[CmdletBinding()]
param(
  [string]$RepoRoot = ".",
  [int]$FromPhase = 0,
  [int]$ThroughPhase = 8,
  [switch]$IncludePhase9,
  [switch]$DryRun,
  [switch]$ReplayExistingCommits,
  [switch]$SkipDiffCheck,
  [switch]$SkipGitWritableCheck,
  [switch]$AllowEmptyPhaseCommits
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
  param(
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}

function Format-ArgumentForDisplay {
  param(
    [AllowNull()][string]$Value
  )

  if ($null -eq $Value) {
    return "''"
  }
  if ($Value -match '^[A-Za-z0-9_./:\\-]+$') {
    return $Value
  }
  return "'" + ($Value -replace "'", "''") + "'"
}

function Format-CommandForDisplay {
  param(
    [string]$Executable,
    [string[]]$Arguments
  )

  $parts = @((Format-ArgumentForDisplay $Executable))
  foreach ($argument in $Arguments) {
    $parts += Format-ArgumentForDisplay $argument
  }
  return ($parts -join " ")
}

function Invoke-Git {
  param(
    [string[]]$Arguments
  )

  $previousErrorActionPreference = $ErrorActionPreference
  try {
    $ErrorActionPreference = "Continue"
    $output = & git @Arguments 2>&1
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }

  foreach ($line in @($output)) {
    Write-Host $line
  }
  if ($exitCode -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $exitCode"
  }
}

function New-Phase {
  param(
    [int]$Number,
    [string]$Message,
    [string[]]$Paths
  )

  return [pscustomobject]@{
    Number = $Number
    Message = $Message
    Paths = $Paths
  }
}

if ($IncludePhase9) {
  $ThroughPhase = 9
}

if ($FromPhase -lt 0 -or $FromPhase -gt 9) {
  throw "FromPhase must be between 0 and 9."
}
if ($ThroughPhase -lt 0 -or $ThroughPhase -gt 9) {
  throw "ThroughPhase must be between 0 and 9."
}
if ($FromPhase -gt $ThroughPhase) {
  throw "FromPhase must be less than or equal to ThroughPhase."
}
if ($ThroughPhase -ge 9 -and !$IncludePhase9) {
  throw "Phase 9 requires -IncludePhase9 because it must only be committed after real-client acceptance evidence is complete."
}

$RepoRoot = Resolve-RepoPath $RepoRoot

$phases = @(
  (New-Phase 0 "Document login parity packet baseline" @(
    "migrationv3\README.md",
    "migrationv3\phase_0_baseline_packet_audit.md",
    "migrationv3\packet_baseline.md"
  )),
  (New-Phase 1 "Inventory legacy login server parity scope" @(
    "migrationv3\phase_1_legacy_login_inventory.md",
    "migrationv3\legacy_scenario_inventory.md"
  )),
  (New-Phase 2 "Bring LoginStream transport behavior toward legacy parity" @(
    "migrationv3\phase_2_loginstream_transport_parity.md",
    "cmake\Eq2Dependencies.cmake",
    "source2\net\include\eq2\net\session.h",
    "source2\net\include\eq2\net\socket_transport.h",
    "source2\net\tests\net_tests.cpp",
    "source2\protocol\CMakeLists.txt",
    "source2\protocol\include\eq2\protocol\interserver_packet.h",
    "source2\protocol\include\eq2\protocol\stream_pipeline.h",
    "source2\protocol\tests\protocol_tests.cpp"
  )),
  (New-Phase 3 "Match legacy encrypted login handshake" @(
    "migrationv3\phase_3_encrypted_login_handshake_parity.md",
    "source2\apps\login_server\main.cpp",
    "source2\login\tests\login_server_tests.cpp",
    "source2\protocol\include\eq2\protocol\login_response.h",
    "source2\protocol\tests\protocol_tests.cpp"
  )),
  (New-Phase 4 "Match legacy login account policy" @(
    "migrationv3\phase_4_account_login_policy_parity.md",
    "source2\db\include\eq2\db\fake_database.h",
    "source2\db\include\eq2\db\repositories.h",
    "source2\db\include\eq2\db\sql_repositories.h",
    "source2\db\tests\db_tests.cpp",
    "source2\login\include\eq2\login\live_login.h",
    "source2\login\tests\login_server_tests.cpp",
    "source2\protocol\include\eq2\protocol\login_request.h"
  )),
  (New-Phase 5 "Match legacy world registration and list behavior" @(
    "migrationv3\phase_5_world_registration_list_parity.md",
    "scripts\source2_live_login_verify.ps1",
    "source2\apps\login_server\main.cpp",
    "source2\login\include\eq2\login\world_registration.h",
    "source2\login\tests\login_server_tests.cpp",
    "source2\protocol\include\eq2\protocol\login_world.h",
    "source2\world\include\eq2\world\live_world.h",
    "source2\world\tests\world_session_tests.cpp"
  )),
  (New-Phase 6 "Match legacy character lifecycle login behavior" @(
    "migrationv3\phase_6_character_lifecycle_parity.md",
    "source2\db\include\eq2\db\fake_database.h",
    "source2\db\include\eq2\db\repositories.h",
    "source2\db\include\eq2\db\sql_repositories.h",
    "source2\db\tests\db_tests.cpp",
    "source2\login\include\eq2\login\live_login.h",
    "source2\login\tests\login_server_tests.cpp",
    "source2\protocol\include\eq2\protocol\create_character.h",
    "source2\protocol\include\eq2\protocol\login_world.h",
    "source2\protocol\tests\protocol_tests.cpp"
  )),
  (New-Phase 7 "Match legacy login admin and client log behavior" @(
    "migrationv3\phase_7_client_logs_admin_web_parity.md",
    "docs\eq2_2006_client_packet_logger_design.md",
    "docs\eq2_2006_client_packet_reversing_notes.md",
    "scripts\analyze_eq2_packet_log.py",
    "scripts\decode_eq2_packet_log.py",
    "scripts\extract_eq2_client_packet_registry.py",
    "scripts\frida_eq2_2006_packet_logger.js",
    "source2\protocol\include\eq2\protocol\client_log.h"
  )),
  (New-Phase 8 "Add login parity diagnostics" @(
    "migrationv3\phase_8_observability_and_diagnostics.md",
    "docs\source2_login_server_guide.md",
    "source2\apps\login_server\main.cpp",
    "source2\login\include\eq2\login\live_login.h",
    "source2\login\tests\login_server_tests.cpp"
  )),
  (New-Phase 9 "Accept source2 login server parity" @(
    ".gitignore",
    "migrationv3\phase_9_full_live_acceptance_gate.md",
    "migrationv3\phase_9_known_issues_and_recommendation.md",
    "migrationv3\packet_diagnostic_comparison.md",
    "migrationv3\real_client_acceptance_report.md",
    "migrationv3\completion_audit.md",
    "migrationv3\commit_status.md",
    "migrationv3\phase_commit_plan.md",
    "migrationv3\git_permission_recovery.md",
    "docs\source2_login_server_guide.md",
    "scripts\source2_run_real_client_acceptance_session.ps1",
    "scripts\source2_run_real_client_acceptance_gate.ps1",
    "scripts\source2_set_eq2_client_login.ps1",
    "scripts\source2_prepare_eq2_client_sandbox.ps1",
    "scripts\source2_record_real_client_acceptance_results.ps1",
    "scripts\source2_audit_real_client_acceptance_session.ps1",
    "scripts\source2_audit_migrationv3_completion.ps1",
    "scripts\source2_diagnose_git_permissions.ps1",
    "scripts\source2_commit_migrationv3_phases.ps1",
    "scripts\source2_check_eq2_client_directx.ps1",
    "scripts\source2_capture_login_packets.ps1",
    "source2\apps\CMakeLists.txt"
  ))
)

$selectedPhases = @($phases | Where-Object { $_.Number -ge $FromPhase -and $_.Number -le $ThroughPhase })

Write-Host "== Source2 migrationv3 phase commit helper =="
Write-Host "Repo root: $RepoRoot"
Write-Host "Phase range: $FromPhase..$ThroughPhase"
if ($ThroughPhase -lt 9) {
  Write-Host "Phase 9 is intentionally excluded; use -IncludePhase9 only after real-client evidence is complete."
}

if ($DryRun) {
  Write-Host "Dry run: no Git state will be changed."
  foreach ($phase in $selectedPhases) {
    Write-Host "Phase $($phase.Number): $($phase.Message)"
    Write-Host (Format-CommandForDisplay -Executable "git" -Arguments (@("-C", $RepoRoot, "add", "--") + $phase.Paths))
    $commitArgs = @("-C", $RepoRoot, "commit", "-m", $phase.Message)
    if ($AllowEmptyPhaseCommits) {
      $commitArgs = @("-C", $RepoRoot, "commit", "--allow-empty", "-m", $phase.Message)
    }
    Write-Host (Format-CommandForDisplay -Executable "git" -Arguments $commitArgs)
  }
  exit 0
}

if (!$SkipGitWritableCheck) {
  $diagnoseScript = Join-Path $RepoRoot "scripts\source2_diagnose_git_permissions.ps1"
  if (!(Test-Path -LiteralPath $diagnoseScript)) {
    throw "Missing Git permission diagnostic helper: $diagnoseScript"
  }
  & powershell -NoProfile -ExecutionPolicy Bypass -File $diagnoseScript -RepoRoot $RepoRoot -NoRecommendations
  if ($LASTEXITCODE -ne 0) {
    throw "Git metadata is not writable. Run scripts\source2_diagnose_git_permissions.ps1 -WriteAclReport for recovery details."
  }
}

if (!$SkipDiffCheck) {
  Invoke-Git @("-C", $RepoRoot, "diff", "--check")
}

$existingSubjects = @(& git -C $RepoRoot log --format=%s -n 500 2>$null)
if ($LASTEXITCODE -ne 0) {
  throw "Unable to inspect existing Git history."
}

foreach ($phase in $selectedPhases) {
  if (!$ReplayExistingCommits -and $existingSubjects -contains $phase.Message) {
    Write-Host "Skipping Phase $($phase.Number); commit already exists: $($phase.Message)"
    continue
  }

  foreach ($path in $phase.Paths) {
    $resolvedPath = Join-Path $RepoRoot $path
    if (!(Test-Path -LiteralPath $resolvedPath)) {
      throw "Phase $($phase.Number) path is missing: $path"
    }
  }

  Write-Host "== Phase $($phase.Number): $($phase.Message) =="
  Invoke-Git (@("-C", $RepoRoot, "add", "--") + $phase.Paths)

  $commitArgs = @("-C", $RepoRoot, "commit", "-m", $phase.Message)
  if ($AllowEmptyPhaseCommits) {
    $commitArgs = @("-C", $RepoRoot, "commit", "--allow-empty", "-m", $phase.Message)
  }
  Invoke-Git $commitArgs
}

Write-Host "migrationv3 phase commit helper completed."
