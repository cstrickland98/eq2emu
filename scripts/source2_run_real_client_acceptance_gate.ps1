[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
  [string[]]$ClientTargetHosts = @("127.0.0.1", "192.168.1.41"),
  [string]$ClientDir = "E:\Games\Everquest II",
  [string]$OutputDir = "artifacts\source2-real-client-acceptance",
  [switch]$UpdateClientConfig,
  [switch]$RestoreClientConfigOnExit,
  [switch]$LaunchClient,
  [switch]$SnapshotClientLogs,
  [switch]$RecordAcceptAll,
  [string]$ForbiddenSecret = "",
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
  param(
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) $Path))
}

function Format-ArgumentForDisplay {
  param(
    [AllowNull()][string]$Value
  )

  if ($null -eq $Value) {
    return "''"
  }
  if ($Value -match '^[A-Za-z0-9_./:\\<>-]+$') {
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

function Invoke-CheckedScript {
  param(
    [string]$Label,
    [string]$ScriptPath,
    [string[]]$Arguments
  )

  Write-Host "== $Label =="
  Write-Host (Format-CommandForDisplay -Executable "powershell" -Arguments (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ScriptPath) + $Arguments))
  $output = & powershell -NoProfile -ExecutionPolicy Bypass -File $ScriptPath @Arguments *>&1
  $exitCode = $LASTEXITCODE
  $output | Write-Host
  if ($exitCode -ne 0) {
    throw "$Label failed with exit code $exitCode"
  }
  return @($output)
}

function Get-SessionDirFromOutput {
  param(
    [object[]]$Output
  )

  foreach ($line in $Output) {
    $text = [string]$line
    if ($text -match '^Session report:\s+(.+session\.md)\s*$') {
      return (Split-Path -Parent $Matches[1])
    }
    if ($text -match '^Incomplete session report:\s+(.+session\.md)\s*$') {
      return (Split-Path -Parent $Matches[1])
    }
  }
  return $null
}

function New-SessionArguments {
  param(
    [string]$HostName
  )

  $args = @(
    "-BuildDir", $BuildDir,
    "-ClientTargetHost", $HostName,
    "-ClientDir", $ClientDir,
    "-OutputDir", $OutputDir
  )
  if ($UpdateClientConfig) { $args += "-UpdateClientConfig" }
  if ($RestoreClientConfigOnExit) { $args += "-RestoreClientConfigOnExit" }
  if ($LaunchClient) { $args += "-LaunchClient" }
  if ($SnapshotClientLogs) { $args += "-SnapshotClientLogs" }
  return $args
}

function New-RecordArguments {
  param(
    [string]$SessionDir
  )

  return @(
    "-SessionDir", $SessionDir,
    "-AcceptAll",
    "-RequireAllRecorded"
  )
}

function New-AuditArguments {
  param(
    [string]$SessionDir
  )

  $args = @(
    "-SessionDir", $SessionDir,
    "-RequireManualPass",
    "-RequireLoginAccepted",
    "-RequireWorldRegistered",
    "-RequireFailedLoginDiagnostic"
  )
  if ($SnapshotClientLogs) {
    $args += "-RequireClientLogSnapshot"
  }
  if (![string]::IsNullOrWhiteSpace($ForbiddenSecret)) {
    $args += @("-ForbiddenSecret", $ForbiddenSecret)
  }
  return $args
}

$sessionScript = Resolve-RepoPath "scripts\source2_run_real_client_acceptance_session.ps1"
$recordScript = Resolve-RepoPath "scripts\source2_record_real_client_acceptance_results.ps1"
$auditScript = Resolve-RepoPath "scripts\source2_audit_real_client_acceptance_session.ps1"

foreach ($script in @($sessionScript, $recordScript, $auditScript)) {
  if (!(Test-Path -LiteralPath $script)) {
    throw "Missing helper script '$script'."
  }
}

$expandedClientTargetHosts = [System.Collections.Generic.List[string]]::new()
foreach ($hostEntry in $ClientTargetHosts) {
  foreach ($hostName in ([string]$hostEntry -split ',')) {
    $trimmed = $hostName.Trim()
    if (![string]::IsNullOrWhiteSpace($trimmed)) {
      [void]$expandedClientTargetHosts.Add($trimmed)
    }
  }
}

if ($expandedClientTargetHosts.Count -eq 0) {
  throw "At least one ClientTargetHosts value is required."
}

if (!$DryRun -and [Console]::IsInputRedirected) {
  throw "Interactive input is redirected; run this gate from an interactive PowerShell window or use -DryRun."
}

if ($DryRun) {
  Write-Host "== Source2 real-client acceptance gate dry run =="
  foreach ($hostName in $expandedClientTargetHosts) {
    $sessionArgs = New-SessionArguments -HostName $hostName
    $sessionPlaceholder = "<session-dir-for-$hostName>"
    Write-Host "Host: $hostName"
    Write-Host "Session command:"
    Write-Host (Format-CommandForDisplay -Executable "powershell" -Arguments (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $sessionScript) + $sessionArgs))
    Write-Host "Record command:"
    Write-Host (Format-CommandForDisplay -Executable "powershell" -Arguments (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $recordScript) + (New-RecordArguments -SessionDir $sessionPlaceholder)))
    Write-Host "Audit command:"
    Write-Host (Format-CommandForDisplay -Executable "powershell" -Arguments (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $auditScript) + (New-AuditArguments -SessionDir $sessionPlaceholder)))
  }
  return
}

foreach ($hostName in $expandedClientTargetHosts) {
  $sessionOutput = Invoke-CheckedScript `
    -Label "Run real-client acceptance session for $hostName" `
    -ScriptPath $sessionScript `
    -Arguments (New-SessionArguments -HostName $hostName)

  $sessionDir = Get-SessionDirFromOutput -Output $sessionOutput
  if ([string]::IsNullOrWhiteSpace($sessionDir)) {
    throw "Could not find the session report path in the wrapper output for $hostName."
  }

  if ($RecordAcceptAll) {
    Invoke-CheckedScript `
      -Label "Record real-client acceptance results for $hostName" `
      -ScriptPath $recordScript `
      -Arguments (New-RecordArguments -SessionDir $sessionDir) | Out-Null

    Invoke-CheckedScript `
      -Label "Audit real-client acceptance evidence for $hostName" `
      -ScriptPath $auditScript `
      -Arguments (New-AuditArguments -SessionDir $sessionDir) | Out-Null
  } else {
    Write-Host "Manual result recording required for ${hostName}:"
    Write-Host (Format-CommandForDisplay -Executable "powershell" -Arguments (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $recordScript) + (New-RecordArguments -SessionDir $sessionDir)))
    Write-Host "Then audit the evidence:"
    Write-Host (Format-CommandForDisplay -Executable "powershell" -Arguments (@("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $auditScript) + (New-AuditArguments -SessionDir $sessionDir)))
  }
}

Write-Host "source2 real-client acceptance gate sequence finished."
