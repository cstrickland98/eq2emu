[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)]
  [string]$SessionDir,
  [ValidateSet("", "Passed", "Failed", "Blocked", "Skipped", "Pending")]
  [string]$Login = "",
  [ValidateSet("", "Passed", "Failed", "Blocked", "Skipped", "Pending")]
  [string]$WorldList = "",
  [ValidateSet("", "Passed", "Failed", "Blocked", "Skipped", "Pending")]
  [string]$CharacterList = "",
  [ValidateSet("", "Passed", "Failed", "Blocked", "Skipped", "Pending")]
  [string]$Play = "",
  [ValidateSet("", "Passed", "Failed", "Blocked", "Skipped", "Pending")]
  [string]$CreateDelete = "",
  [ValidateSet("", "Passed", "Failed", "Blocked", "Skipped", "Pending")]
  [string]$FailedLoginDiagnostic = "",
  [string]$LoginNotes = "",
  [string]$WorldListNotes = "",
  [string]$CharacterListNotes = "",
  [string]$PlayNotes = "",
  [string]$CreateDeleteNotes = "",
  [string]$FailedLoginDiagnosticNotes = "",
  [switch]$AcceptAll,
  [switch]$RequireAllRecorded,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Escape-MarkdownTableCell {
  param(
    [AllowNull()][string]$Value
  )

  if ($null -eq $Value) {
    return ""
  }
  return ([string]$Value) -replace '\|', '\|'
}

function Add-ResultUpdate {
  param(
    [hashtable]$Updates,
    [string]$Key,
    [string]$Result,
    [string]$Notes
  )

  if (![string]::IsNullOrWhiteSpace($Result) -or ![string]::IsNullOrWhiteSpace($Notes)) {
    $Updates[$Key] = [pscustomobject]@{
      Result = $Result
      Notes = $Notes
    }
  }
}

function Get-GateKey {
  param(
    [string]$Gate
  )

  if ($Gate -like "Login progresses past*") {
    return "login"
  }
  if ($Gate -like "World list shows*") {
    return "world_list"
  }
  if ($Gate -like "Character list appears*") {
    return "character_list"
  }
  if ($Gate -like "Play/select reaches*") {
    return "play"
  }
  if ($Gate -like "Create/delete test character*") {
    return "create_delete"
  }
  if ($Gate -like "Wrong-password attempt logs*") {
    return "failed_login_diagnostic"
  }
  return ""
}

$resolvedSessionDir = [System.IO.Path]::GetFullPath($SessionDir)
$sessionPath = Join-Path $resolvedSessionDir "session.md"
if (!(Test-Path -LiteralPath $sessionPath)) {
  throw "Missing session report '$sessionPath'."
}

if ($AcceptAll) {
  foreach ($name in @("Login", "WorldList", "CharacterList", "Play", "CreateDelete", "FailedLoginDiagnostic")) {
    if ([string]::IsNullOrWhiteSpace((Get-Variable -Name $name -ValueOnly))) {
      Set-Variable -Name $name -Value "Passed"
    }
  }
}

$updates = @{}
Add-ResultUpdate $updates "login" $Login $LoginNotes
Add-ResultUpdate $updates "world_list" $WorldList $WorldListNotes
Add-ResultUpdate $updates "character_list" $CharacterList $CharacterListNotes
Add-ResultUpdate $updates "play" $Play $PlayNotes
Add-ResultUpdate $updates "create_delete" $CreateDelete $CreateDeleteNotes
Add-ResultUpdate $updates "failed_login_diagnostic" $FailedLoginDiagnostic $FailedLoginDiagnosticNotes

if ($updates.Count -eq 0 -and !$RequireAllRecorded) {
  throw "No result updates were requested."
}

$lines = @(Get-Content -LiteralPath $sessionPath)
$newLines = [System.Collections.Generic.List[string]]::new()
$seen = @{}
$remainingPending = [System.Collections.Generic.List[string]]::new()

foreach ($line in $lines) {
  if ($line -notmatch '^\|') {
    [void]$newLines.Add($line)
    continue
  }

  if ($line -match '^\|\s*-+\s*\|') {
    [void]$newLines.Add($line)
    continue
  }

  $columns = @($line -split '\|')
  if ($columns.Count -lt 4) {
    [void]$newLines.Add($line)
    continue
  }

  $gate = $columns[1].Trim()
  $result = $columns[2].Trim()
  $notes = $columns[3].Trim()

  if ($gate -eq "Gate" -and $result -eq "Result") {
    [void]$newLines.Add($line)
    continue
  }

  $key = Get-GateKey $gate
  if ([string]::IsNullOrWhiteSpace($key)) {
    [void]$newLines.Add($line)
    continue
  }

  $seen[$key] = $true
  if ($updates.ContainsKey($key)) {
    $update = $updates[$key]
    if (![string]::IsNullOrWhiteSpace($update.Result)) {
      $result = $update.Result
    }
    if (![string]::IsNullOrWhiteSpace($update.Notes)) {
      $notes = $update.Notes
    }
  }

  if ($RequireAllRecorded -and ($result -eq "Pending" -or [string]::IsNullOrWhiteSpace($result))) {
    [void]$remainingPending.Add($gate)
  }

  [void]$newLines.Add(("| {0} | {1} | {2} |" -f $gate,(Escape-MarkdownTableCell $result),(Escape-MarkdownTableCell $notes)))
}

foreach ($key in @("login", "world_list", "character_list", "play", "create_delete", "failed_login_diagnostic")) {
  if (!$seen.ContainsKey($key)) {
    throw "Session report is missing manual gate '$key'."
  }
}

if ($remainingPending.Count -gt 0) {
  Write-Host "source2 real-client acceptance results are incomplete:"
  foreach ($gate in $remainingPending) {
    Write-Host " - $gate"
  }
  exit 1
}

if ($DryRun) {
  Write-Host "source2 real-client acceptance result dry run."
  Write-Host "Would update: $sessionPath"
  foreach ($key in ($updates.Keys | Sort-Object)) {
    $update = $updates[$key]
    Write-Host (" - {0}: result={1} notes={2}" -f $key,$update.Result,$update.Notes)
  }
  return
}

Set-Content -LiteralPath $sessionPath -Value $newLines
Write-Host "source2 real-client acceptance results recorded."
Write-Host "Session: $sessionPath"
