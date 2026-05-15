[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
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
  [string]$Configuration = "Debug",
  [string]$OutputDir = "artifacts\source2-target-live-gate",
  [string]$TcpScriptPath = "",
  [string]$CtestExe = "ctest",
  [switch]$NoTranscript,
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

function Resolve-RepoPath {
  param(
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }

  $repoRoot = Split-Path -Parent $PSScriptRoot
  return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

function Test-EnvSecret {
  param(
    [string]$Name
  )

  $value = [System.Environment]::GetEnvironmentVariable($Name)
  return ![string]::IsNullOrWhiteSpace($value)
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

function Format-CtestCommandForDisplay {
  param(
    [string[]]$Arguments
  )

  $parts = @("ctest")
  foreach ($argument in $Arguments) {
    $parts += Format-ArgumentForDisplay $argument
  }
  return $parts -join " "
}

function Format-EnvAssignmentForDisplay {
  param(
    [string]$Name,
    [string]$Value
  )

  return '$env:' + $Name + ' = ' + (Format-ArgumentForDisplay $Value)
}

function Require-EnvSecret {
  param(
    [string]$Name
  )

  if (!(Test-EnvSecret $Name)) {
    throw "Required environment secret $Name is not set."
  }
}

function Stop-GateTranscript {
  if ($script:transcriptStarted) {
    Stop-Transcript | Out-Null
    $script:transcriptStarted = $false
  }
}

if ($ExpectedWorldCount -lt 0) {
  $ExpectedWorldCount = if ($RunWorldServe) { 1 } else { 0 }
}

$BuildDir = Resolve-RepoPath $BuildDir

$tcpScript = if ([string]::IsNullOrWhiteSpace($TcpScriptPath)) {
  Resolve-RepoScript "source2_check_mariadb_tcp.ps1"
} else {
  $TcpScriptPath
}
if (!(Test-Path -LiteralPath $tcpScript)) {
  throw "Missing TCP helper script '$tcpScript'."
}

$ctestArgs = @(
  "--test-dir", $BuildDir,
  "-C", $Configuration,
  "-R", "source2_target_live_login_ctest",
  "--output-on-failure"
)

$targetArgs = @(
  "-BuildDir", $BuildDir,
  "-DbHost", $DbHost,
  "-DbPort", "$DbPort",
  "-LoginDbName", $LoginDbName,
  "-WorldDbName", $WorldDbName,
  "-DbUser", $DbUser,
  "-LoginUsername", $LoginUsername,
  "-ClientVersion", "$ClientVersion",
  "-ExpectedWorldCount", "$ExpectedWorldCount",
  "-LoginPort", "$LoginPort",
  "-WorldPort", "$WorldPort",
  "-WorldName", $WorldName,
  "-WorldAdvertisedAddress", $WorldAdvertisedAddress,
  "-WorldServerVersion", $WorldServerVersion
)
if ($RunWorldServe) {
  $targetArgs += "-RunWorldServe"
}

$targetEnvOverrides = [ordered]@{
  EQ2_SOURCE2_TARGET_BUILD_DIR = $BuildDir
  EQ2_SOURCE2_TARGET_DB_HOST = $DbHost
  EQ2_SOURCE2_TARGET_DB_PORT = "$DbPort"
  EQ2_SOURCE2_TARGET_LOGIN_DB = $LoginDbName
  EQ2_SOURCE2_TARGET_WORLD_DB = $WorldDbName
  EQ2_SOURCE2_TARGET_DB_USER = $DbUser
  EQ2_SOURCE2_TARGET_LOGIN_USERNAME = $LoginUsername
  EQ2_SOURCE2_TARGET_CLIENT_VERSION = "$ClientVersion"
  EQ2_SOURCE2_TARGET_EXPECTED_WORLD_COUNT = "$ExpectedWorldCount"
  EQ2_SOURCE2_TARGET_LOGIN_PORT = "$LoginPort"
  EQ2_SOURCE2_TARGET_WORLD_PORT = "$WorldPort"
  EQ2_SOURCE2_TARGET_WORLD_NAME = $WorldName
  EQ2_SOURCE2_TARGET_WORLD_ADVERTISED_ADDRESS = $WorldAdvertisedAddress
  EQ2_SOURCE2_TARGET_WORLD_SERVER_VERSION = $WorldServerVersion
}

$script:transcriptStarted = $false
$transcriptPath = $null
$exitCode = 0

if (!$DryRun -and !$NoTranscript) {
  New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
  $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
  $transcriptPath = Join-Path $OutputDir "source2-target-live-gate-$timestamp.log"
  Start-Transcript -Path $transcriptPath -Force | Out-Null
  $script:transcriptStarted = $true
}

try {
  Write-Host "== Source2 target live login gate =="
  if ($script:transcriptStarted) {
    Write-Host "Transcript: $transcriptPath"
  }
  Write-Host "MariaDB: $DbHost`:$DbPort"
  Write-Host "Login DB: $LoginDbName"
  Write-Host "World DB: $WorldDbName"
  Write-Host "DB user: $DbUser"
  Write-Host "Login username: $LoginUsername"
  Write-Host "Client version: $ClientVersion"
  Write-Host "RunWorldServe: $RunWorldServe"
  Write-Host "ExpectedWorldCount: $ExpectedWorldCount"
  Write-Host "Required secrets: EQ2_DB_PASSWORD, EQ2_LOGIN_PASSWORD"
  if ($RunWorldServe) {
    Write-Host "Required world secrets: EQ2_WORLD_ACCOUNT, EQ2_WORLD_PASSWORD"
  }

  if ($DryRun) {
    Write-Host "== Dry run commands =="
    Write-Host (Format-PowerShellFileCommandForDisplay -ScriptPath $tcpScript -Arguments @("-DbHost", $DbHost, "-DbPort", "$DbPort"))
    Write-Host '$env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN = ''1'''
    if ($RunWorldServe) {
      Write-Host '$env:EQ2_SOURCE2_RUN_TARGET_WORLD_LIST = ''1'''
    }
    foreach ($entry in $targetEnvOverrides.GetEnumerator()) {
      Write-Host (Format-EnvAssignmentForDisplay -Name $entry.Key -Value ([string]$entry.Value))
    }
    Write-Host "source2_target_live_login_ctest.ps1 parameters:"
    Write-Host ($targetArgs -join " ")
    if ($CtestExe -eq "ctest") {
      Write-Host (Format-CtestCommandForDisplay -Arguments $ctestArgs)
    } else {
      $ctestDisplay = @((Format-ArgumentForDisplay $CtestExe))
      foreach ($argument in $ctestArgs) {
        $ctestDisplay += Format-ArgumentForDisplay $argument
      }
      Write-Host ($ctestDisplay -join " ")
    }
  } else {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $tcpScript -DbHost $DbHost -DbPort $DbPort
    $exitCode = $LASTEXITCODE

    if ($exitCode -eq 0) {
      Require-EnvSecret "EQ2_DB_PASSWORD"
      Require-EnvSecret "EQ2_LOGIN_PASSWORD"
      if ($RunWorldServe) {
        Require-EnvSecret "EQ2_WORLD_ACCOUNT"
        Require-EnvSecret "EQ2_WORLD_PASSWORD"
      }

      $previousRunTarget = $env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN
      $previousRunWorldList = $env:EQ2_SOURCE2_RUN_TARGET_WORLD_LIST
      $previousTargetOverrides = @{}

      try {
        $env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN = "1"
        if ($RunWorldServe) {
          $env:EQ2_SOURCE2_RUN_TARGET_WORLD_LIST = "1"
        } else {
          $env:EQ2_SOURCE2_RUN_TARGET_WORLD_LIST = $null
        }
        foreach ($entry in $targetEnvOverrides.GetEnumerator()) {
          $previousTargetOverrides[$entry.Key] = [System.Environment]::GetEnvironmentVariable($entry.Key)
          Set-Item -Path "Env:$($entry.Key)" -Value ([string]$entry.Value)
        }

        Write-Host "== Running CTest target live login gate =="
        if ($CtestExe -eq "ctest") {
          Write-Host (Format-CtestCommandForDisplay -Arguments $ctestArgs)
        } else {
          $ctestDisplay = @((Format-ArgumentForDisplay $CtestExe))
          foreach ($argument in $ctestArgs) {
            $ctestDisplay += Format-ArgumentForDisplay $argument
          }
          Write-Host ($ctestDisplay -join " ")
        }
        & $CtestExe @ctestArgs
        $exitCode = $LASTEXITCODE
        if ($exitCode -eq 0) {
          Write-Host "source2 target live login gate passed."
        }
      } finally {
        $env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN = $previousRunTarget
        $env:EQ2_SOURCE2_RUN_TARGET_WORLD_LIST = $previousRunWorldList
        foreach ($entry in $previousTargetOverrides.GetEnumerator()) {
          if ($null -eq $entry.Value) {
            Remove-Item -Path "Env:$($entry.Key)" -ErrorAction SilentlyContinue
          } else {
            Set-Item -Path "Env:$($entry.Key)" -Value $entry.Value
          }
        }
      }
    }
  }
} finally {
  Stop-GateTranscript
}

exit $exitCode
