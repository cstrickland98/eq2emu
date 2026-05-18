[CmdletBinding()]
param(
  [string]$RepoRoot = "",
  [string]$MigrationDir = "migrationv3",
  [string[]]$SessionDirs = @(),
  [switch]$RequirePhaseCommits,
  [switch]$RequireRealClientEvidence,
  [switch]$RequireNoBlockedStatus,
  [string[]]$RequiredClientTargetHosts = @("127.0.0.1", "192.168.1.41"),
  [string]$ForbiddenSecret = "",
  [switch]$AllowKnownBlockers
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
  param(
    [string]$Base,
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Add-Issue {
  param(
    [System.Collections.Generic.List[string]]$Issues,
    [string]$Message
  )

  [void]$Issues.Add($Message)
}

function Test-FileContains {
  param(
    [string]$Path,
    [string]$Pattern
  )

  if (!(Test-Path -LiteralPath $Path)) {
    return $false
  }
  return (Get-Content -LiteralPath $Path -Raw) -match $Pattern
}

function Invoke-AcceptanceAudit {
  param(
    [string]$ScriptPath,
    [string]$SessionDir,
    [string]$ForbiddenSecret
  )

  $acceptanceArgs = @(
    "-SessionDir", $SessionDir,
    "-RequireManualPass",
    "-RequireLoginAccepted",
    "-RequireWorldRegistered",
    "-RequireFailedLoginDiagnostic",
    "-RequireClientLogSnapshot"
  )
  if (![string]::IsNullOrEmpty($ForbiddenSecret)) {
    $acceptanceArgs += @("-ForbiddenSecret", $ForbiddenSecret)
  }

  $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptPath `
    @acceptanceArgs *>&1
  $exitCode = $LASTEXITCODE
  return [pscustomobject]@{
    ExitCode = $exitCode
    Text = [string]::Join([Environment]::NewLine, $output)
  }
}

function Get-SessionClientTargetHost {
  param(
    [string]$SessionDir
  )

  $sessionPath = Join-Path $SessionDir "session.md"
  if (!(Test-Path -LiteralPath $sessionPath)) {
    return $null
  }

  foreach ($line in (Get-Content -LiteralPath $sessionPath)) {
    if ($line -match '^- Client login setting:\s*`+cl_ls_address\s+(.+?)`+\s*$') {
      return $Matches[1].Trim()
    }
  }

  return $null
}

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
  $RepoRoot = Split-Path -Parent $PSScriptRoot
}
$RepoRoot = [System.IO.Path]::GetFullPath($RepoRoot)
$migrationRoot = Resolve-RepoPath $RepoRoot $MigrationDir

$structuralIssues = [System.Collections.Generic.List[string]]::new()
$completionIssues = [System.Collections.Generic.List[string]]::new()

$requiredFiles = @(
  "README.md",
  "phase_0_baseline_packet_audit.md",
  "phase_1_legacy_login_inventory.md",
  "phase_2_loginstream_transport_parity.md",
  "phase_3_encrypted_login_handshake_parity.md",
  "phase_4_account_login_policy_parity.md",
  "phase_5_world_registration_list_parity.md",
  "phase_6_character_lifecycle_parity.md",
  "phase_7_client_logs_admin_web_parity.md",
  "phase_8_observability_and_diagnostics.md",
  "phase_9_full_live_acceptance_gate.md",
  "legacy_scenario_inventory.md",
  "packet_baseline.md",
  "packet_diagnostic_comparison.md",
  "real_client_acceptance_report.md",
  "phase_9_known_issues_and_recommendation.md",
  "completion_audit.md",
  "commit_status.md",
  "phase_commit_plan.md",
  "git_permission_recovery.md"
)

$phaseCommitMessages = @(
  "Document login parity packet baseline",
  "Inventory legacy login server parity scope",
  "Bring LoginStream transport behavior toward legacy parity",
  "Match legacy encrypted login handshake",
  "Match legacy login account policy",
  "Match legacy world registration and list behavior",
  "Match legacy character lifecycle login behavior",
  "Match legacy login admin and client log behavior",
  "Add login parity diagnostics",
  "Accept source2 login server parity"
)

foreach ($file in $requiredFiles) {
  $path = Join-Path $migrationRoot $file
  if (!(Test-Path -LiteralPath $path)) {
    Add-Issue $structuralIssues "Missing migration artifact: $path"
  }
}

foreach ($phase in 0..9) {
  $phaseFile = Get-ChildItem -LiteralPath $migrationRoot -Filter ("phase_{0}_*.md" -f $phase) -ErrorAction SilentlyContinue | Select-Object -First 1
  if ($null -eq $phaseFile) {
    Add-Issue $structuralIssues "Missing phase $phase file."
    continue
  }
  if (!(Test-FileContains -Path $phaseFile.FullName -Pattern '## Git Commit')) {
    Add-Issue $structuralIssues "Phase $phase file does not include a Git Commit section."
  }
  if (!(Test-FileContains -Path $phaseFile.FullName -Pattern '(?m)^\s*git\s+add\b')) {
    Add-Issue $structuralIssues "Phase $phase file does not include a git add command."
  }
  $expectedMessage = $phaseCommitMessages[$phase]
  $expectedCommitPattern = '(?m)^\s*git\s+commit\s+-m\s+"' + [regex]::Escape($expectedMessage) + '"\s*$'
  if (!(Test-FileContains -Path $phaseFile.FullName -Pattern $expectedCommitPattern)) {
    Add-Issue $structuralIssues "Phase $phase file does not include the required commit command: $expectedMessage"
  }
}

$requiredScripts = @(
  "scripts\source2_run_real_client_acceptance_session.ps1",
  "scripts\source2_run_real_client_acceptance_gate.ps1",
  "scripts\source2_record_real_client_acceptance_results.ps1",
  "scripts\source2_audit_real_client_acceptance_session.ps1",
  "scripts\source2_audit_migrationv3_completion.ps1",
  "scripts\source2_prepare_eq2_client_sandbox.ps1",
  "scripts\source2_set_eq2_client_login.ps1",
  "scripts\source2_diagnose_git_permissions.ps1",
  "scripts\source2_commit_migrationv3_phases.ps1",
  "scripts\source2_check_eq2_client_directx.ps1",
  "scripts\source2_capture_login_packets.ps1"
)

foreach ($script in $requiredScripts) {
  $path = Resolve-RepoPath $RepoRoot $script
  if (!(Test-Path -LiteralPath $path)) {
    Add-Issue $structuralIssues "Missing required helper script: $path"
  }
}

if ($RequireNoBlockedStatus) {
  $statusFiles = @(
    "README.md",
    "phase_0_baseline_packet_audit.md",
    "phase_1_legacy_login_inventory.md",
    "phase_2_loginstream_transport_parity.md",
    "phase_3_encrypted_login_handshake_parity.md",
    "phase_4_account_login_policy_parity.md",
    "phase_5_world_registration_list_parity.md",
    "phase_6_character_lifecycle_parity.md",
    "phase_7_client_logs_admin_web_parity.md",
    "phase_8_observability_and_diagnostics.md",
    "phase_9_full_live_acceptance_gate.md",
    "real_client_acceptance_report.md",
    "phase_9_known_issues_and_recommendation.md"
  )

  foreach ($file in $statusFiles) {
    $path = Join-Path $migrationRoot $file
    if (!(Test-Path -LiteralPath $path)) {
      continue
    }
    $text = Get-Content -LiteralPath $path -Raw
    if ($text -match '(?im)^Status:\s*(blocked|pending|partial|interim)\b') {
      Add-Issue $completionIssues "Incomplete status remains in $file."
    }
    if ($text -match '\|\s*Pending\s*\|') {
      Add-Issue $completionIssues "Pending table result remains in $file."
    }
  }
}

if ($RequirePhaseCommits) {
  $subjects = @(& git -C $RepoRoot log --format=%s -n 300 2>$null)
  if ($LASTEXITCODE -ne 0) {
    Add-Issue $completionIssues "Unable to inspect git history for required phase commits."
  } else {
    foreach ($message in $phaseCommitMessages) {
      if ($subjects -notcontains $message) {
        Add-Issue $completionIssues "Missing required phase commit: $message"
      }
    }
  }
}

if ($RequireRealClientEvidence) {
  if ($SessionDirs.Count -eq 0) {
    Add-Issue $completionIssues "No real-client acceptance SessionDirs were provided."
  } else {
    $auditScript = Resolve-RepoPath $RepoRoot "scripts\source2_audit_real_client_acceptance_session.ps1"
    $observedClientTargets = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    foreach ($sessionDir in $SessionDirs) {
      $resolvedSession = Resolve-RepoPath $RepoRoot $sessionDir
      $audit = Invoke-AcceptanceAudit -ScriptPath $auditScript -SessionDir $resolvedSession -ForbiddenSecret $ForbiddenSecret
      if ($audit.ExitCode -ne 0) {
        Add-Issue $completionIssues "Real-client evidence audit failed for '$resolvedSession': $($audit.Text)"
      }

      $clientTargetHost = Get-SessionClientTargetHost -SessionDir $resolvedSession
      if ([string]::IsNullOrWhiteSpace($clientTargetHost)) {
        Add-Issue $completionIssues "Real-client evidence '$resolvedSession' does not record a client target host."
      } else {
        [void]$observedClientTargets.Add($clientTargetHost)
      }
    }

    foreach ($requiredHost in $RequiredClientTargetHosts) {
      if (![string]::IsNullOrWhiteSpace($requiredHost) -and !$observedClientTargets.Contains($requiredHost)) {
        Add-Issue $completionIssues "Missing real-client evidence for required client target host: $requiredHost"
      }
    }
  }
}

if ($structuralIssues.Count -gt 0) {
  Write-Host "migrationv3 completion audit failed structural checks:"
  foreach ($issue in $structuralIssues) {
    Write-Host " - $issue"
  }
  exit 1
}

if ($completionIssues.Count -gt 0) {
  if ($AllowKnownBlockers) {
    Write-Host "migrationv3 completion audit found known blockers:"
    foreach ($issue in $completionIssues) {
      Write-Host " - $issue"
    }
    Write-Host "Known blockers accepted by -AllowKnownBlockers."
    exit 0
  }

  Write-Host "migrationv3 is not complete:"
  foreach ($issue in $completionIssues) {
    Write-Host " - $issue"
  }
  exit 1
}

Write-Host "migrationv3 completion audit passed."
