[CmdletBinding()]
param(
  [string]$ClientDir = "E:\Games\Everquest II",
  [string]$ConfigPath = "",
  [string]$LoginHost = "127.0.0.1",
  [switch]$RestoreOriginal,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-ClientConfig {
  param(
    [string]$ClientDir,
    [string]$ConfigPath
  )

  if (![string]::IsNullOrWhiteSpace($ConfigPath)) {
    return [System.IO.Path]::GetFullPath($ConfigPath)
  }

  if ([string]::IsNullOrWhiteSpace($ClientDir)) {
    throw "ClientDir is required when ConfigPath is not provided."
  }

  return [System.IO.Path]::GetFullPath((Join-Path $ClientDir "eq2_default.ini"))
}

function Get-CurrentLoginHost {
  param(
    [string[]]$Lines
  )

  foreach ($line in $Lines) {
    if ($line -match '^\s*cl_ls_address\s+(.+?)\s*$') {
      return $Matches[1]
    }
  }
  return $null
}

function Set-LoginHostLine {
  param(
    [string[]]$Lines,
    [string]$LoginHost
  )

  $result = [System.Collections.Generic.List[string]]::new()
  $updated = $false
  foreach ($line in $Lines) {
    if ($line -match '^\s*cl_ls_address\b') {
      if (!$updated) {
        [void]$result.Add("cl_ls_address $LoginHost")
        $updated = $true
      }
      continue
    }
    [void]$result.Add($line)
  }

  if (!$updated) {
    $result.Insert(0, "cl_ls_address $LoginHost")
  }

  return $result.ToArray()
}

if (!$RestoreOriginal -and [string]::IsNullOrWhiteSpace($LoginHost)) {
  throw "LoginHost is required unless -RestoreOriginal is used."
}

if (!$RestoreOriginal -and $LoginHost -match ':') {
  throw "LoginHost must be a host or IPv4 address without a port for the legacy EQ2 client."
}

$config = Resolve-ClientConfig -ClientDir $ClientDir -ConfigPath $ConfigPath
$backup = "$config.source2-backup"

if ($RestoreOriginal) {
  if (!(Test-Path -LiteralPath $backup)) {
    throw "Missing backup '$backup'. Cannot restore original login host."
  }

  if ($DryRun) {
    Write-Host "Would restore '$config' from '$backup'."
    return
  }

  Copy-Item -LiteralPath $backup -Destination $config -Force
  Write-Host "Restored '$config' from '$backup'."
  return
}

if (!(Test-Path -LiteralPath $config)) {
  throw "Missing EQ2 client config '$config'."
}

$lines = @(Get-Content -LiteralPath $config)
$current = Get-CurrentLoginHost -Lines $lines
$updatedLines = Set-LoginHostLine -Lines $lines -LoginHost $LoginHost

if ($DryRun) {
  if ($null -eq $current) {
    Write-Host "Would add cl_ls_address $LoginHost to '$config'."
  } else {
    Write-Host "Would change '$config' from cl_ls_address $current to cl_ls_address $LoginHost."
  }
  if (!(Test-Path -LiteralPath $backup)) {
    Write-Host "Would create backup '$backup'."
  }
  return
}

if (!(Test-Path -LiteralPath $backup)) {
  Copy-Item -LiteralPath $config -Destination $backup
}

Set-Content -LiteralPath $config -Value $updatedLines
Write-Host "Set '$config' to cl_ls_address $LoginHost."
Write-Host "Backup: $backup"
