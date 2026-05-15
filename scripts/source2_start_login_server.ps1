[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
  [string]$ConfigPath = "source2\config\login_server.eq2emu-target.ini.example",
  [string]$DbHost = "192.168.1.243",
  [int]$DbPort = 3306,
  [string]$DbName = "eq2ls",
  [string]$DbUser = "eq2emu",
  [string]$DbPassword = "",
  [string]$LoginUsername = "testlabs",
  [string]$LoginPassword = "",
  [int]$ClientVersion = 546,
  [switch]$SkipDbPreflight,
  [switch]$SkipAccountPreflight,
  [switch]$DryRun,
  [int]$RunForMs = 0
)

$ErrorActionPreference = "Stop"

function Resolve-LoginServer {
  $path = Join-Path $BuildDir "source2\apps\Debug\eq2_login_server.exe"
  if (!(Test-Path -LiteralPath $path)) {
    throw "Missing eq2_login_server.exe at '$path'. Build first with scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir $BuildDir"
  }
  return $path
}

function Invoke-Checked {
  param(
    [string]$Label,
    [scriptblock]$Command
  )

  Write-Host "== $Label =="
  & $Command
  if ($LASTEXITCODE -ne 0) {
    throw "$Label failed with exit code $LASTEXITCODE"
  }
}

function Resolve-Secret {
  param(
    [string]$Value,
    [string]$EnvName,
    [string]$ParamName
  )

  if (![string]::IsNullOrWhiteSpace($Value)) {
    return $Value
  }
  $envValue = [System.Environment]::GetEnvironmentVariable($EnvName)
  if (![string]::IsNullOrWhiteSpace($envValue)) {
    return $envValue
  }
  throw "$ParamName or $EnvName is required unless -DryRun is used."
}

function Get-IniValue {
  param(
    [string]$Path,
    [string]$Section,
    [string]$Key
  )

  $currentSection = ""
  foreach ($line in Get-Content -LiteralPath $Path) {
    $trimmed = $line.Trim()
    if ([string]::IsNullOrWhiteSpace($trimmed) -or $trimmed.StartsWith(";") -or $trimmed.StartsWith("#")) {
      continue
    }
    if ($trimmed -match '^\[(.+)\]$') {
      $currentSection = $Matches[1].Trim()
      continue
    }
    if ($currentSection -eq $Section -and $trimmed -match '^([^=]+)=(.*)$') {
      if ($Matches[1].Trim() -eq $Key) {
        return $Matches[2].Trim()
      }
    }
  }

  return $null
}

function Test-ConfiguredSecret {
  param(
    [AllowNull()][string]$Value
  )

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $false
  }

  $normalized = $Value.Trim().ToLowerInvariant()
  return $normalized -notin @("change-me", "changeme", "placeholder", "<db-password>", "<password>")
}

function Resolve-SecretOrConfig {
  param(
    [string]$Value,
    [string]$EnvName,
    [string]$ParamName,
    [string]$ConfigPath,
    [string]$ConfigSection,
    [string]$ConfigKey,
    [string]$ConfigLabel
  )

  if (![string]::IsNullOrWhiteSpace($Value)) {
    return [pscustomobject]@{ Source = "argument"; Value = $Value }
  }
  $envValue = [System.Environment]::GetEnvironmentVariable($EnvName)
  if (![string]::IsNullOrWhiteSpace($envValue)) {
    return [pscustomobject]@{ Source = "environment"; Value = $envValue }
  }
  $configValue = Get-IniValue -Path $ConfigPath -Section $ConfigSection -Key $ConfigKey
  if (Test-ConfiguredSecret $configValue) {
    return [pscustomobject]@{ Source = "config"; Value = $null }
  }
  throw "$ParamName, $EnvName, or $ConfigLabel in '$ConfigPath' is required unless -DryRun is used."
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
  return "& " + ($parts -join " ")
}

function Get-TcpFailureDetail {
  param(
    [string]$TargetHost,
    [int]$TargetPort,
    [int]$TimeoutMs = 3000
  )

  $client = $null
  try {
    $client = [System.Net.Sockets.TcpClient]::new()
    $async = $client.BeginConnect($TargetHost, $TargetPort, $null, $null)
    if (!$async.AsyncWaitHandle.WaitOne($TimeoutMs, $false)) {
      return "Timeout: TCP connect timed out after $TimeoutMs ms."
    }

    try {
      $client.EndConnect($async)
      return "Connected."
    } catch {
      if ($_.Exception.InnerException -and ![string]::IsNullOrWhiteSpace($_.Exception.InnerException.Message)) {
        return $_.Exception.InnerException.Message
      }
      return $_.Exception.Message
    }
  } catch {
    if ($_.Exception.InnerException -and ![string]::IsNullOrWhiteSpace($_.Exception.InnerException.Message)) {
      return $_.Exception.InnerException.Message
    }
    return $_.Exception.Message
  } finally {
    if ($client) {
      $client.Close()
    }
  }
}

function Test-CodexSandboxFirewallRule {
  param(
    [string]$RuleName
  )

  try {
    $output = & netsh advfirewall firewall show rule name=$RuleName verbose 2>$null
    if ($LASTEXITCODE -ne 0) {
      return $false
    }
    $text = [string]::Join([Environment]::NewLine, @($output))
    return ($text -match "Enabled:\s+Yes" -and
            $text -match "Direction:\s+Out" -and
            $text -match "Action:\s+Block")
  } catch {
    return $false
  }
}

function Write-CodexSandboxTcpHint {
  param(
    [string]$TargetHost
  )

  $isLoopback = ($TargetHost -eq "localhost" -or $TargetHost -like "127.*")
  if (!$isLoopback -and (Test-CodexSandboxFirewallRule "codex_sandbox_offline_block_outbound")) {
    Write-Host "Detected Codex sandbox offline outbound firewall rule; rerun from a normal PowerShell prompt or a network-enabled Codex session."
  }
  if ($isLoopback -and (Test-CodexSandboxFirewallRule "codex_sandbox_offline_block_loopback_tcp")) {
    Write-Host "Detected Codex sandbox offline loopback TCP firewall rule; rerun from a normal PowerShell prompt or a network-enabled Codex session."
  }
}

if (!(Test-Path -LiteralPath $ConfigPath)) {
  throw "Missing config '$ConfigPath'. Copy source2\config\login_server.eq2emu-target.ini.example outside source control and replace password placeholders."
}

$loginServer = Resolve-LoginServer

Invoke-Checked "Validate login config" {
  & $loginServer --validate-config --config $ConfigPath
}

$serveArgs = @(
  "--serve",
  "--config", $ConfigPath,
  "--db-host", $DbHost,
  "--db-port", "$DbPort",
  "--db-name", $DbName,
  "--db-user", $DbUser,
  "--client-version", "$ClientVersion"
)

if ($RunForMs -gt 0) {
  $serveArgs += @("--run-for-ms", "$RunForMs")
}

$checkArgs = @()
$checkLabel = ""
if (!$SkipDbPreflight) {
  $checkArgs = @(
    "--check-login-db",
    "--config", $ConfigPath,
    "--db-host", $DbHost,
    "--db-port", "$DbPort",
    "--db-name", $DbName,
    "--db-user", $DbUser,
    "--client-version", "$ClientVersion"
  )
  $checkLabel = "Login DB/account/opcode check"
  if ($SkipAccountPreflight) {
    $checkArgs += "--skip-account-auth"
    $checkLabel = "Login DB/opcode check"
  }
}

if ($DryRun) {
  Write-Host "== Source2 login server dry run =="
  if (!$SkipDbPreflight -and !$SkipAccountPreflight) {
    Write-Host "Login username: $LoginUsername"
  }
  Write-Host "Required environment secrets unless provided by config:"
  $listedSecret = $false
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $ConfigPath -Section "db" -Key "password"))) {
    Write-Host "  EQ2_DB_PASSWORD"
    $listedSecret = $true
  }
  if (!$SkipDbPreflight -and !$SkipAccountPreflight) {
    Write-Host "  EQ2_LOGIN_PASSWORD"
    $listedSecret = $true
  }
  if (!$listedSecret) {
    Write-Host "  (none)"
  }
  if (!$SkipDbPreflight) {
    Write-Host "Planned preflight:"
    Write-Host "  $checkLabel"
    Write-Host (Format-CommandForDisplay -Executable $loginServer -Arguments $checkArgs)
  } else {
    Write-Host "Planned preflight: skipped by -SkipDbPreflight"
  }
  Write-Host "Planned serve:"
  Write-Host (Format-CommandForDisplay -Executable $loginServer -Arguments $serveArgs)
  return
}

if (!$SkipDbPreflight) {
  Write-Host "== MariaDB TCP reachability =="
  $tcp = Test-NetConnection -ComputerName $DbHost -Port $DbPort
  if (!$tcp.TcpTestSucceeded) {
    $detail = Get-TcpFailureDetail -TargetHost $DbHost -TargetPort $DbPort
    if (![string]::IsNullOrWhiteSpace($detail)) {
      Write-Host "TCP failure detail: $detail"
    }
    if ($detail -match "forbidden|access permissions|permission denied") {
      Write-CodexSandboxTcpHint -TargetHost $DbHost
    }
    throw "Cannot reach $DbHost`:$DbPort from this host. Fix source2 outbound policy and/or MariaDB bind/firewall/grants before starting source2 login."
  }
}

$DbPasswordSecret = Resolve-SecretOrConfig `
  -Value $DbPassword `
  -EnvName "EQ2_DB_PASSWORD" `
  -ParamName "-DbPassword" `
  -ConfigPath $ConfigPath `
  -ConfigSection "db" `
  -ConfigKey "password" `
  -ConfigLabel "db.password"
if (!$SkipDbPreflight -and !$SkipAccountPreflight) {
  $LoginPassword = Resolve-Secret -Value $LoginPassword -EnvName "EQ2_LOGIN_PASSWORD" -ParamName "-LoginPassword"
}

$previousDbPassword = $env:EQ2_DB_PASSWORD
$previousLoginUsername = $env:EQ2_LOGIN_USERNAME
$previousLoginPassword = $env:EQ2_LOGIN_PASSWORD
if ($DbPasswordSecret.Source -ne "config") {
  $env:EQ2_DB_PASSWORD = $DbPasswordSecret.Value
} else {
  $env:EQ2_DB_PASSWORD = $null
}
if (!$SkipDbPreflight -and !$SkipAccountPreflight) {
  $env:EQ2_LOGIN_USERNAME = $LoginUsername
  $env:EQ2_LOGIN_PASSWORD = $LoginPassword
}

try {
  if (!$SkipDbPreflight) {
    Invoke-Checked $checkLabel {
      & $loginServer @checkArgs
    }
  }

  $env:EQ2_LOGIN_USERNAME = $null
  $env:EQ2_LOGIN_PASSWORD = $null

  Write-Host "== Starting source2 login server =="
  Write-Host (Format-CommandForDisplay -Executable $loginServer -Arguments $serveArgs)
  & $loginServer @serveArgs
  $serveExitCode = $LASTEXITCODE
  if ($serveExitCode -ne 0) {
    throw "source2 login server exited with code $serveExitCode"
  }
} finally {
  $env:EQ2_DB_PASSWORD = $previousDbPassword
  $env:EQ2_LOGIN_USERNAME = $previousLoginUsername
  $env:EQ2_LOGIN_PASSWORD = $previousLoginPassword
}
