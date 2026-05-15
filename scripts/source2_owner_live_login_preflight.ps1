[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
  [string]$OutputDir = "artifacts\source2-live-login-preflight",
  [string]$DbHost = "192.168.1.243",
  [int]$DbPort = 3306,
  [string]$LoginDbName = "eq2ls",
  [string]$WorldDbName = "eq2emu",
  [string]$DbUser = "eq2emu",
  [string]$LoginUsername = "testlabs",
  [int]$ClientVersion = 546,
  [switch]$RunWorldServe,
  [int]$ExpectedWorldCount = -1,
  [int]$LoginPort = 9100,
  [int]$WorldPort = 9101,
  [string]$WorldName = "Source2 World",
  [string]$WorldAdvertisedAddress = "127.0.0.1",
  [string]$WorldServerVersion = "source2",
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-RepoScript {
  param(
    [string]$Name
  )

  $repoRoot = Split-Path -Parent $PSScriptRoot
  $path = Join-Path $repoRoot "scripts\$Name"
  if (!(Test-Path -LiteralPath $path)) {
    throw "Missing script '$path'."
  }
  return $path
}

function Test-EnvSecret {
  param(
    [string]$Name
  )

  $value = [System.Environment]::GetEnvironmentVariable($Name)
  return ![string]::IsNullOrWhiteSpace($value)
}

function Write-SecretStatus {
  param(
    [string]$Name
  )

  if (Test-EnvSecret $Name) {
    Write-Host "${Name}: set"
  } else {
    Write-Host "${Name}: missing"
  }
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
  return "& " + ($parts -join " ")
}

function Format-PowerShellFileCommandForDisplay {
  param(
    [string]$ScriptPath,
    [string[]]$Arguments
  )

  $parts = @(
    "powershell",
    "-NoProfile",
    "-ExecutionPolicy",
    "Bypass",
    "-File",
    (Format-ArgumentForDisplay $ScriptPath)
  )
  foreach ($argument in $Arguments) {
    $parts += Format-ArgumentForDisplay $argument
  }
  return $parts -join " "
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

$verifyScript = Resolve-RepoScript "source2_live_login_verify.ps1"

if ($ExpectedWorldCount -lt 0) {
  $ExpectedWorldCount = if ($RunWorldServe) { 1 } else { 0 }
}

$gateName = if ($RunWorldServe) { "world-list" } else { "login-only" }

$verifyArgs = @(
  "-BuildDir", $BuildDir,
  "-DbHost", $DbHost,
  "-DbPort", "$DbPort",
  "-LoginDbName", $LoginDbName,
  "-WorldDbName", $WorldDbName,
  "-DbUser", $DbUser,
  "-LoginUsername", $LoginUsername,
  "-ClientVersion", "$ClientVersion",
  "-RunMariaDbSmoke",
  "-RunServeProbe",
  "-ExpectedWorldCount", "$ExpectedWorldCount",
  "-LoginPort", "$LoginPort",
  "-WorldPort", "$WorldPort",
  "-WorldName", $WorldName,
  "-WorldAdvertisedAddress", $WorldAdvertisedAddress,
  "-WorldServerVersion", $WorldServerVersion
)

$verifyParams = @{
  BuildDir = $BuildDir
  DbHost = $DbHost
  DbPort = $DbPort
  LoginDbName = $LoginDbName
  WorldDbName = $WorldDbName
  DbUser = $DbUser
  LoginUsername = $LoginUsername
  ClientVersion = $ClientVersion
  RunMariaDbSmoke = $true
  RunServeProbe = $true
  ExpectedWorldCount = $ExpectedWorldCount
  LoginPort = $LoginPort
  WorldPort = $WorldPort
  WorldName = $WorldName
  WorldAdvertisedAddress = $WorldAdvertisedAddress
  WorldServerVersion = $WorldServerVersion
}

if ($RunWorldServe) {
  $verifyArgs += "-RunWorldServe"
  $verifyParams.RunWorldServe = $true
}

if ($DryRun) {
  Write-Host "== Source2 owner live login preflight dry run =="
  Write-Host "Gate: $gateName"
  Write-Host "Required environment secrets:"
  Write-Host "  EQ2_DB_PASSWORD"
  Write-Host "  EQ2_LOGIN_PASSWORD"
  if ($RunWorldServe) {
    Write-Host "  EQ2_WORLD_ACCOUNT"
    Write-Host "  EQ2_WORLD_PASSWORD"
  }
  Write-Host (Format-PowerShellFileCommandForDisplay -ScriptPath $verifyScript -Arguments $verifyArgs)
  return
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$transcriptPath = Join-Path $OutputDir "source2-live-login-preflight-$timestamp.log"

$exitCode = 0
Start-Transcript -Path $transcriptPath -Force | Out-Null
try {
  Write-Host "== Source2 owner live login preflight =="
  Write-Host "Timestamp: $(Get-Date -Format o)"
  Write-Host "BuildDir: $BuildDir"
  Write-Host "MariaDB: $DbHost`:$DbPort"
  Write-Host "Login DB: $LoginDbName"
  Write-Host "World DB: $WorldDbName"
  Write-Host "DB user: $DbUser"
  Write-Host "Login username: $LoginUsername"
  Write-Host "Client version: $ClientVersion"
  Write-Host "RunWorldServe: $RunWorldServe"
  Write-Host "ExpectedWorldCount: $ExpectedWorldCount"
  Write-Host "Gate: $gateName"
  Write-Host "Environment secret status:"
  Write-SecretStatus "EQ2_DB_PASSWORD"
  Write-SecretStatus "EQ2_LOGIN_PASSWORD"
  if ($RunWorldServe) {
    Write-SecretStatus "EQ2_WORLD_ACCOUNT"
    Write-SecretStatus "EQ2_WORLD_PASSWORD"
  }

  Write-Host "== Source2 host TCP check =="
  Test-NetConnection -ComputerName $DbHost -Port $DbPort

  Write-Host "== Source2 live verifier =="
  Write-Host (Format-PowerShellFileCommandForDisplay -ScriptPath $verifyScript -Arguments $verifyArgs)
  & $verifyScript @verifyParams
} catch {
  $exitCode = 1
  Write-Host "FAILED: $($_.Exception.Message)"
} finally {
  Write-Host "Transcript: $transcriptPath"
  Stop-Transcript | Out-Null
}

exit $exitCode
