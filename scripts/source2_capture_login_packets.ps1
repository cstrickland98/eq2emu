[CmdletBinding()]
param(
  [int]$LoginPort = 9100,
  [string]$TargetHost = "",
  [int]$DurationSeconds = 120,
  [int]$PacketSize = 0,
  [string]$OutputDir = "artifacts\source2-login-packet-capture",
  [string]$CaptureName = "",
  [string]$PktMonExe = "pktmon",
  [switch]$NoConvert,
  [switch]$ResetFilters,
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

function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Invoke-PktMonChecked {
  param(
    [string]$Label,
    [string[]]$Arguments
  )

  Write-Host "== $Label =="
  Write-Host (Format-CommandForDisplay -Executable $PktMonExe -Arguments $Arguments)
  $output = & $PktMonExe @Arguments *>&1
  $exitCode = $LASTEXITCODE
  $output | Write-Host
  if ($exitCode -ne 0) {
    throw "$Label failed with exit code $exitCode"
  }
}

if ($LoginPort -lt 1 -or $LoginPort -gt 65535) {
  throw "LoginPort must be an integer from 1 to 65535."
}
if ($DurationSeconds -lt 1) {
  throw "DurationSeconds must be at least 1."
}
if ($PacketSize -lt 0) {
  throw "PacketSize must be 0 or greater."
}

$resolvedOutputDir = Resolve-RepoPath $OutputDir
if ([string]::IsNullOrWhiteSpace($CaptureName)) {
  $CaptureName = "source2-login-udp-$LoginPort-" + (Get-Date -Format "yyyyMMdd-HHmmss")
}

$safeCaptureName = $CaptureName -replace '[^A-Za-z0-9_.-]', '_'
$captureDir = Join-Path $resolvedOutputDir $safeCaptureName
$etlPath = Join-Path $captureDir "$safeCaptureName.etl"
$pcapPath = Join-Path $captureDir "$safeCaptureName.pcapng"
$notesPath = Join-Path $captureDir "capture.md"

$filterArgs = @("filter", "add", "source2-login-$LoginPort", "-t", "UDP", "-p", "$LoginPort")
if (![string]::IsNullOrWhiteSpace($TargetHost)) {
  $filterArgs += @("-i", $TargetHost)
}
$startArgs = @("start", "--capture", "--pkt-size", "$PacketSize", "--file-name", $etlPath)
$stopArgs = @("stop")
$convertArgs = @("etl2pcap", $etlPath, "--out", $pcapPath)
$removeArgs = @("filter", "remove")

if ($DryRun) {
  Write-Host "== Source2 login packet capture dry run =="
  Write-Host "Output directory: $captureDir"
  Write-Host "Capture duration: $DurationSeconds seconds"
  Write-Host "UDP login port: $LoginPort"
  if (![string]::IsNullOrWhiteSpace($TargetHost)) {
    Write-Host "Target host filter: $TargetHost"
  }
  if ($ResetFilters) {
    Write-Host (Format-CommandForDisplay -Executable $PktMonExe -Arguments $removeArgs)
  }
  Write-Host (Format-CommandForDisplay -Executable $PktMonExe -Arguments $filterArgs)
  Write-Host (Format-CommandForDisplay -Executable $PktMonExe -Arguments $startArgs)
  Write-Host "Start the EQ2 client login attempt while capture is running."
  Write-Host (Format-CommandForDisplay -Executable $PktMonExe -Arguments $stopArgs)
  if (!$NoConvert) {
    Write-Host (Format-CommandForDisplay -Executable $PktMonExe -Arguments $convertArgs)
  }
  Write-Host "Capture report: $notesPath"
  return
}

if (!(Get-Command $PktMonExe -ErrorAction SilentlyContinue)) {
  throw "Missing '$PktMonExe'. pktmon is included with modern Windows builds; install or use Wireshark/tshark manually."
}
if (!(Test-IsAdministrator)) {
  throw "pktmon capture requires an elevated PowerShell session. Re-run this script as Administrator, or capture UDP $LoginPort with Wireshark/tshark."
}

New-Item -ItemType Directory -Force -Path $captureDir | Out-Null

$started = $false
try {
  if ($ResetFilters) {
    Invoke-PktMonChecked -Label "Reset pktmon filters" -Arguments $removeArgs
  }
  Invoke-PktMonChecked -Label "Add source2 login UDP filter" -Arguments $filterArgs
  Invoke-PktMonChecked -Label "Start pktmon capture" -Arguments $startArgs
  $started = $true

  Write-Host "Capture running for $DurationSeconds seconds. Start the EQ2 client login attempt now."
  Start-Sleep -Seconds $DurationSeconds

  Invoke-PktMonChecked -Label "Stop pktmon capture" -Arguments $stopArgs
  $started = $false

  if (!$NoConvert) {
    Invoke-PktMonChecked -Label "Convert pktmon ETL to pcapng" -Arguments $convertArgs
  }

  $targetHostLabel = if ([string]::IsNullOrWhiteSpace($TargetHost)) { "none" } else { $TargetHost }
  $pcapLabel = if ($NoConvert) { "not converted" } else { $pcapPath }
  $lines = @(
    "# Source2 Login Packet Capture",
    "",
    "- Started: $(Get-Date -Format o)",
    "- UDP login port: ``$LoginPort``",
    "- Target host filter: ``$targetHostLabel``",
    "- Duration seconds: ``$DurationSeconds``",
    "- Packet size: ``$PacketSize``",
    "- ETL: ``$etlPath``",
    "- PCAPNG: ``$pcapLabel``",
    "",
    "Use Wireshark display filter ``udp.port == $LoginPort`` and compare with",
    "``E:\_EQ2\packets\sourcehex.txt`` / ``source2hex.txt``. For Phase 9, the",
    "important question is whether the real client sends the encrypted login",
    "request after the initial session/key exchange."
  )
  Set-Content -LiteralPath $notesPath -Value $lines
  Write-Host "Capture report: $notesPath"
} finally {
  if ($started) {
    try {
      & $PktMonExe @stopArgs | Write-Host
    } catch {
      Write-Host "Warning: failed to stop pktmon capture: $($_.Exception.Message)"
    }
  }
}
