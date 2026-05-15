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
  [string]$WorldServerVersion = "source2"
)

$ErrorActionPreference = "Stop"

function Test-Truthy {
  param(
    [AllowNull()][string]$Value
  )

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $false
  }

  return $Value -match '^(1|true|yes|on)$'
}

function Get-EnvOverride {
  param(
    [string]$Name
  )

  $value = [System.Environment]::GetEnvironmentVariable($Name)
  if ([string]::IsNullOrWhiteSpace($value)) {
    return $null
  }
  return $value
}

function Use-EnvString {
  param(
    [string]$Current,
    [string]$Name
  )

  $value = Get-EnvOverride $Name
  if ($null -eq $value) {
    return $Current
  }
  return $value
}

function Use-EnvInt {
  param(
    [int]$Current,
    [string]$Name
  )

  $value = Get-EnvOverride $Name
  if ($null -eq $value) {
    return $Current
  }

  $parsed = 0
  if (![int]::TryParse($value, [ref]$parsed)) {
    throw "$Name must be an integer."
  }
  return $parsed
}

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

if (!(Test-Truthy $env:EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN)) {
  Write-Host "SKIP: set EQ2_SOURCE2_RUN_TARGET_LIVE_LOGIN=1 to run the target DB-backed source2 live login CTest."
  return
}

$BuildDir = Use-EnvString $BuildDir "EQ2_SOURCE2_TARGET_BUILD_DIR"
$DbHost = Use-EnvString $DbHost "EQ2_SOURCE2_TARGET_DB_HOST"
$DbPort = Use-EnvInt $DbPort "EQ2_SOURCE2_TARGET_DB_PORT"
$LoginDbName = Use-EnvString $LoginDbName "EQ2_SOURCE2_TARGET_LOGIN_DB"
$WorldDbName = Use-EnvString $WorldDbName "EQ2_SOURCE2_TARGET_WORLD_DB"
$DbUser = Use-EnvString $DbUser "EQ2_SOURCE2_TARGET_DB_USER"
$LoginUsername = Use-EnvString $LoginUsername "EQ2_SOURCE2_TARGET_LOGIN_USERNAME"
$ClientVersion = Use-EnvInt $ClientVersion "EQ2_SOURCE2_TARGET_CLIENT_VERSION"
$ExpectedWorldCount = Use-EnvInt $ExpectedWorldCount "EQ2_SOURCE2_TARGET_EXPECTED_WORLD_COUNT"
$LoginPort = Use-EnvInt $LoginPort "EQ2_SOURCE2_TARGET_LOGIN_PORT"
$WorldPort = Use-EnvInt $WorldPort "EQ2_SOURCE2_TARGET_WORLD_PORT"
$WorldName = Use-EnvString $WorldName "EQ2_SOURCE2_TARGET_WORLD_NAME"
$WorldAdvertisedAddress = Use-EnvString $WorldAdvertisedAddress "EQ2_SOURCE2_TARGET_WORLD_ADVERTISED_ADDRESS"
$WorldServerVersion = Use-EnvString $WorldServerVersion "EQ2_SOURCE2_TARGET_WORLD_SERVER_VERSION"

$runWorldList = $RunWorldServe -or (Test-Truthy $env:EQ2_SOURCE2_RUN_TARGET_WORLD_LIST)
if ($ExpectedWorldCount -lt 0) {
  $ExpectedWorldCount = if ($runWorldList) { 1 } else { 0 }
}

$verifyScript = Resolve-RepoScript "source2_live_login_verify.ps1"

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

if ($runWorldList) {
  $verifyParams.RunWorldServe = $true
}

Write-Host "== Source2 target live login CTest =="
Write-Host "MariaDB: $DbHost`:$DbPort"
Write-Host "Login DB: $LoginDbName"
Write-Host "World DB: $WorldDbName"
Write-Host "DB user: $DbUser"
Write-Host "Login username: $LoginUsername"
Write-Host "Client version: $ClientVersion"
Write-Host "RunWorldServe: $runWorldList"
Write-Host "ExpectedWorldCount: $ExpectedWorldCount"
Write-Host "Required secrets: EQ2_DB_PASSWORD, EQ2_LOGIN_PASSWORD"
if ($runWorldList) {
  Write-Host "Required world secrets: EQ2_WORLD_ACCOUNT, EQ2_WORLD_PASSWORD"
}

& $verifyScript @verifyParams
