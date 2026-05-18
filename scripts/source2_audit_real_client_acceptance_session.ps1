[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SessionDir,
  [switch]$RequireManualPass,
  [switch]$RequireLoginAccepted,
  [switch]$RequireClientLogSnapshot,
  [switch]$RequireWorldRegistered,
  [switch]$RequireFailedLoginDiagnostic,
  [int]$ExpectedRegisteredWorlds = 1,
  [string]$ForbiddenSecret = ""
)

$ErrorActionPreference = "Stop"

function Resolve-SessionPath {
  param(
    [string]$BaseDir,
    [AllowNull()][string]$Path
  )

  if ([string]::IsNullOrWhiteSpace($Path) -or $Path -eq "not requested") {
    return $null
  }
  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path $BaseDir $Path))
}

function Get-SessionValue {
  param(
    [string[]]$Content,
    [string]$Label
  )

  $pattern = '^- ' + [regex]::Escape($Label) + ': `+(.+?)`+$'
  foreach ($line in $Content) {
    if ($line -match $pattern) {
      return $Matches[1]
    }
  }
  return $null
}

function Read-ExistingText {
  param(
    [string[]]$Paths
  )

  $parts = [System.Collections.Generic.List[string]]::new()
  foreach ($path in $Paths) {
    if (![string]::IsNullOrWhiteSpace($path) -and (Test-Path -LiteralPath $path)) {
      [void]$parts.Add((Get-Content -LiteralPath $path -Raw))
    }
  }
  return [string]::Join([Environment]::NewLine, $parts)
}

function Add-Failure {
  param(
    [System.Collections.Generic.List[string]]$Failures,
    [string]$Message
  )

  [void]$Failures.Add($Message)
}

if ($ExpectedRegisteredWorlds -lt 0) {
  throw "ExpectedRegisteredWorlds must be zero or greater."
}

$resolvedSessionDir = [System.IO.Path]::GetFullPath($SessionDir)
$sessionPath = Join-Path $resolvedSessionDir "session.md"
$failures = [System.Collections.Generic.List[string]]::new()

if (!(Test-Path -LiteralPath $sessionPath)) {
  throw "Missing session report '$sessionPath'."
}

$session = Get-Content -LiteralPath $sessionPath
$sessionText = [string]::Join([Environment]::NewLine, $session)
$loginStdOut = Resolve-SessionPath $resolvedSessionDir (Get-SessionValue $session "Login stdout")
$loginStdErr = Resolve-SessionPath $resolvedSessionDir (Get-SessionValue $session "Login stderr")
$worldStdOut = Resolve-SessionPath $resolvedSessionDir (Get-SessionValue $session "World stdout")
$worldStdErr = Resolve-SessionPath $resolvedSessionDir (Get-SessionValue $session "World stderr")
$clientLogSnapshotDir = Resolve-SessionPath $resolvedSessionDir (Get-SessionValue $session "Client log snapshots")

$requiredLogs = @(
  @{ Label = "Login stdout"; Path = $loginStdOut },
  @{ Label = "Login stderr"; Path = $loginStdErr },
  @{ Label = "World stdout"; Path = $worldStdOut },
  @{ Label = "World stderr"; Path = $worldStdErr }
)

foreach ($log in $requiredLogs) {
  if ([string]::IsNullOrWhiteSpace($log.Path)) {
    Add-Failure $failures "Session report is missing $($log.Label)."
  } elseif (!(Test-Path -LiteralPath $log.Path)) {
    Add-Failure $failures "Session report references missing $($log.Label): $($log.Path)"
  }
}

$logText = Read-ExistingText @($loginStdOut, $loginStdErr, $worldStdOut, $worldStdErr)

if (![string]::IsNullOrEmpty($ForbiddenSecret) -and $sessionText.Contains($ForbiddenSecret)) {
  Add-Failure $failures "Session report contains the forbidden secret value."
}

if ($RequireLoginAccepted -and $logText -notmatch "login_accepted") {
  Add-Failure $failures "Source2 logs do not contain a login_accepted event."
}

if ($RequireWorldRegistered) {
  $hasWorldRegisteredEvent = $logText -match "world_registered"
  $registeredWorlds = $null
  if ($logText -match 'registered_worlds"?\s*[:=]\s*"?([0-9]+)') {
    $registeredWorlds = [int]$Matches[1]
  }

  if (!$hasWorldRegisteredEvent) {
    Add-Failure $failures "Source2 logs do not contain a world_registered event."
  }
  if ($null -eq $registeredWorlds) {
    Add-Failure $failures "Source2 logs do not contain a registered_worlds count."
  } elseif ($registeredWorlds -lt $ExpectedRegisteredWorlds) {
    Add-Failure $failures "Source2 logs show registered_worlds=$registeredWorlds; expected at least $ExpectedRegisteredWorlds."
  }
}

if ($RequireFailedLoginDiagnostic) {
  if ($logText -notmatch "login_rejected" -and $logText -notmatch "login_failures") {
    Add-Failure $failures "Source2 logs do not contain failed-login diagnostics."
  }
  if (![string]::IsNullOrEmpty($ForbiddenSecret) -and $logText.Contains($ForbiddenSecret)) {
    Add-Failure $failures "Source2 logs contain the forbidden secret value."
  }
}

if ($RequireClientLogSnapshot) {
  if ([string]::IsNullOrWhiteSpace($clientLogSnapshotDir)) {
    Add-Failure $failures "Session report does not reference a client log snapshot directory."
  } elseif (!(Test-Path -LiteralPath $clientLogSnapshotDir)) {
    Add-Failure $failures "Client log snapshot directory is missing: $clientLogSnapshotDir"
  } else {
    $manifestPath = Join-Path $clientLogSnapshotDir "manifest.tsv"
    if (!(Test-Path -LiteralPath $manifestPath)) {
      Add-Failure $failures "Client log snapshot manifest is missing: $manifestPath"
    } else {
      $manifest = Get-Content -LiteralPath $manifestPath
      $existingRows = @($manifest | Where-Object { $_.Contains("`tTrue`t") -or $_.Contains('`tTrue`t') })
      if ($existingRows.Count -eq 0) {
        Add-Failure $failures "Client log snapshot manifest does not list any copied client logs."
      }
    }

    if (![string]::IsNullOrEmpty($ForbiddenSecret)) {
      $snapshotFiles = @(Get-ChildItem -LiteralPath $clientLogSnapshotDir -File -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName)
      $snapshotText = Read-ExistingText $snapshotFiles
      if ($snapshotText.Contains($ForbiddenSecret)) {
        Add-Failure $failures "Client log snapshots contain the forbidden secret value."
      }
    }
  }
}

if ($RequireManualPass) {
  $accepted = @("pass", "passed", "ok", "yes", "accepted")
  $gateRows = 0
  foreach ($line in $session) {
    if ($line -notmatch '^\|') {
      continue
    }
    if ($line -match '^\|\s*-+\s*\|') {
      continue
    }

    $columns = @($line -split '\|')
    if ($columns.Count -lt 4) {
      continue
    }

    $gate = $columns[1].Trim()
    $result = $columns[2].Trim()
    if ($gate -eq "Gate" -and $result -eq "Result") {
      continue
    }
    if ([string]::IsNullOrWhiteSpace($gate)) {
      continue
    }

    $gateRows += 1
    $normalized = $result.ToLowerInvariant()
    if ($accepted -notcontains $normalized) {
      Add-Failure $failures "Manual gate '$gate' is '$result', not passed."
    }
  }

  if ($gateRows -eq 0) {
    Add-Failure $failures "Session report does not contain manual gate rows."
  }
}

if ($failures.Count -gt 0) {
  Write-Host "source2 real-client acceptance evidence audit failed:"
  foreach ($failure in $failures) {
    Write-Host " - $failure"
  }
  exit 1
}

Write-Host "source2 real-client acceptance evidence audit passed."
Write-Host "Session: $sessionPath"
if (![string]::IsNullOrWhiteSpace($clientLogSnapshotDir)) {
  Write-Host "Client log snapshots: $clientLogSnapshotDir"
}
