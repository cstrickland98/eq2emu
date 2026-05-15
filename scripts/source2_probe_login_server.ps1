[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
  [string]$ConfigPath = "source2\config\login_server.eq2emu-target.ini.example",
  [string]$ConnectHost = "127.0.0.1",
  [int]$ConnectPort = 9100,
  [string]$DbHost = "192.168.1.243",
  [int]$DbPort = 3306,
  [string]$DbName = "eq2ls",
  [string]$DbUser = "eq2emu",
  [string]$DbPassword = "",
  [string]$LoginUsername = "testlabs",
  [string]$LoginPassword = "",
  [int]$ClientVersion = 546,
  [int]$ExpectedWorldCount = -1,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-LoginServer {
  $path = Join-Path $BuildDir "source2\apps\Debug\eq2_login_server.exe"
  if (!(Test-Path -LiteralPath $path)) {
    throw "Missing eq2_login_server.exe at '$path'. Build first with scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir $BuildDir"
  }
  return $path
}

function Format-CommandForDisplay {
  param(
    [string]$Executable,
    [string[]]$Arguments
  )

  $masked = @()
  for ($index = 0; $index -lt $Arguments.Count; ++$index) {
    $masked += $Arguments[$index]
    if ($Arguments[$index] -eq "--password" -and ($index + 1) -lt $Arguments.Count) {
      ++$index
      $masked += "<login-password>"
    }
  }

  $parts = @((Format-ArgumentForDisplay $Executable))
  foreach ($argument in $masked) {
    $parts += Format-ArgumentForDisplay $argument
  }
  return "& " + ($parts -join " ")
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

$probeArgs = @(
  "--probe-login",
  "--config", $ConfigPath,
  "--connect-host", $ConnectHost,
  "--connect-port", "$ConnectPort",
  "--db-host", $DbHost,
  "--db-port", "$DbPort",
  "--db-name", $DbName,
  "--db-user", $DbUser,
  "--client-version", "$ClientVersion"
)

if ($ExpectedWorldCount -ge 0) {
  $probeArgs += @("--expect-world-count", "$ExpectedWorldCount")
}

if ($DryRun) {
  Write-Host "== Source2 login probe dry run =="
  Write-Host "Login username: $LoginUsername"
  Write-Host "Required environment secrets unless provided by config:"
  $listedSecret = $false
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $ConfigPath -Section "db" -Key "password"))) {
    Write-Host "  EQ2_DB_PASSWORD"
    $listedSecret = $true
  }
  Write-Host "  EQ2_LOGIN_PASSWORD"
  $listedSecret = $true
  if (!$listedSecret) {
    Write-Host "  (none)"
  }
  Write-Host (Format-CommandForDisplay -Executable $loginServer -Arguments $probeArgs)
  return
}

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
  throw "Cannot reach $DbHost`:$DbPort from this host. Fix source2 outbound policy and/or MariaDB bind/firewall/grants before probing source2 login."
}

$DbPasswordSecret = Resolve-SecretOrConfig `
  -Value $DbPassword `
  -EnvName "EQ2_DB_PASSWORD" `
  -ParamName "-DbPassword" `
  -ConfigPath $ConfigPath `
  -ConfigSection "db" `
  -ConfigKey "password" `
  -ConfigLabel "db.password"
$LoginPassword = Resolve-Secret -Value $LoginPassword -EnvName "EQ2_LOGIN_PASSWORD" -ParamName "-LoginPassword"

$previousDbPassword = $env:EQ2_DB_PASSWORD
$previousLoginUsername = $env:EQ2_LOGIN_USERNAME
$previousLoginPassword = $env:EQ2_LOGIN_PASSWORD
if ($DbPasswordSecret.Source -ne "config") {
  $env:EQ2_DB_PASSWORD = $DbPasswordSecret.Value
} else {
  $env:EQ2_DB_PASSWORD = $null
}
$env:EQ2_LOGIN_USERNAME = $LoginUsername
$env:EQ2_LOGIN_PASSWORD = $LoginPassword

try {
  Write-Host "== Probing source2 login server =="
  Write-Host (Format-CommandForDisplay -Executable $loginServer -Arguments $probeArgs)
  & $loginServer @probeArgs
  $probeExitCode = $LASTEXITCODE
  if ($probeExitCode -ne 0) {
    throw "source2 login probe exited with code $probeExitCode"
  }
} finally {
  $env:EQ2_DB_PASSWORD = $previousDbPassword
  $env:EQ2_LOGIN_USERNAME = $previousLoginUsername
  $env:EQ2_LOGIN_PASSWORD = $previousLoginPassword
}
