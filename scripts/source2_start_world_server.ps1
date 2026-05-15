[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
  [string]$ConfigPath = "source2\config\world_server.eq2emu-target.ini.example",
  [string]$DbHost = "192.168.1.243",
  [int]$DbPort = 3306,
  [string]$DbName = "eq2emu",
  [string]$DbUser = "eq2emu",
  [string]$DbPassword = "",
  [string]$LoginHost = "127.0.0.1",
  [int]$LoginPort = 9100,
  [string]$WorldAccount = "",
  [string]$WorldPassword = "",
  [switch]$SkipDbPreflight,
  [switch]$SkipLoginReachability,
  [switch]$DryRun,
  [int]$RunForMs = 0
)

$ErrorActionPreference = "Stop"

function Resolve-WorldServer {
  $path = Join-Path $BuildDir "source2\apps\Debug\eq2_world_server.exe"
  if (!(Test-Path -LiteralPath $path)) {
    throw "Missing eq2_world_server.exe at '$path'. Build first with scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir $BuildDir"
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
  return $normalized -notin @("change-me", "changeme", "placeholder", "<db-password>", "<world-account>", "<world-password>", "<password>")
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
  throw "Missing config '$ConfigPath'. Copy source2\config\world_server.eq2emu-target.ini.example outside source control and replace password placeholders."
}

$worldServer = Resolve-WorldServer

Invoke-Checked "Validate world config" {
  & $worldServer --validate-config --config $ConfigPath
}

$serveArgs = @(
  "--serve",
  "--config", $ConfigPath,
  "--login-address", $LoginHost,
  "--login-port", "$LoginPort",
  "--db-host", $DbHost,
  "--db-port", "$DbPort",
  "--db-name", $DbName,
  "--db-user", $DbUser
)

if ($RunForMs -gt 0) {
  $serveArgs += @("--run-for-ms", "$RunForMs")
}

if ($DryRun) {
  Write-Host "== Source2 world server dry run =="
  Write-Host "Required environment secrets unless provided by config:"
  $listedSecret = $false
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $ConfigPath -Section "world_db" -Key "password"))) {
    Write-Host "  EQ2_DB_PASSWORD"
    $listedSecret = $true
  }
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $ConfigPath -Section "world" -Key "account"))) {
    Write-Host "  EQ2_WORLD_ACCOUNT"
    $listedSecret = $true
  }
  if (!(Test-ConfiguredSecret (Get-IniValue -Path $ConfigPath -Section "world" -Key "password"))) {
    Write-Host "  EQ2_WORLD_PASSWORD"
    $listedSecret = $true
  }
  if (!$listedSecret) {
    Write-Host "  (none)"
  }
  Write-Host (Format-CommandForDisplay -Executable $worldServer -Arguments $serveArgs)
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
    throw "Cannot reach $DbHost`:$DbPort from this host. Fix source2 outbound policy and/or MariaDB bind/firewall/grants before starting source2 world."
  }
}

if (!$SkipLoginReachability) {
  Write-Host "== Login TCP reachability =="
  $loginTcp = Test-NetConnection -ComputerName $LoginHost -Port $LoginPort
  if (!$loginTcp.TcpTestSucceeded) {
    $detail = Get-TcpFailureDetail -TargetHost $LoginHost -TargetPort $LoginPort
    if (![string]::IsNullOrWhiteSpace($detail)) {
      Write-Host "TCP failure detail: $detail"
    }
    if ($detail -match "forbidden|access permissions|permission denied") {
      Write-CodexSandboxTcpHint -TargetHost $LoginHost
    }
    throw "Cannot reach source2 login at $LoginHost`:$LoginPort. Start login first and allow TCP on the login port."
  }
}

$DbPasswordSecret = Resolve-SecretOrConfig `
  -Value $DbPassword `
  -EnvName "EQ2_DB_PASSWORD" `
  -ParamName "-DbPassword" `
  -ConfigPath $ConfigPath `
  -ConfigSection "world_db" `
  -ConfigKey "password" `
  -ConfigLabel "world_db.password"
$WorldAccountSecret = Resolve-SecretOrConfig `
  -Value $WorldAccount `
  -EnvName "EQ2_WORLD_ACCOUNT" `
  -ParamName "-WorldAccount" `
  -ConfigPath $ConfigPath `
  -ConfigSection "world" `
  -ConfigKey "account" `
  -ConfigLabel "world.account"
$WorldPasswordSecret = Resolve-SecretOrConfig `
  -Value $WorldPassword `
  -EnvName "EQ2_WORLD_PASSWORD" `
  -ParamName "-WorldPassword" `
  -ConfigPath $ConfigPath `
  -ConfigSection "world" `
  -ConfigKey "password" `
  -ConfigLabel "world.password"

$previousDbPassword = $env:EQ2_DB_PASSWORD
$previousWorldAccount = $env:EQ2_WORLD_ACCOUNT
$previousWorldPassword = $env:EQ2_WORLD_PASSWORD
if ($DbPasswordSecret.Source -ne "config") {
  $env:EQ2_DB_PASSWORD = $DbPasswordSecret.Value
} else {
  $env:EQ2_DB_PASSWORD = $null
}
if ($WorldAccountSecret.Source -ne "config") {
  $env:EQ2_WORLD_ACCOUNT = $WorldAccountSecret.Value
} else {
  $env:EQ2_WORLD_ACCOUNT = $null
}
if ($WorldPasswordSecret.Source -ne "config") {
  $env:EQ2_WORLD_PASSWORD = $WorldPasswordSecret.Value
} else {
  $env:EQ2_WORLD_PASSWORD = $null
}

try {
  if (!$SkipDbPreflight) {
    Invoke-Checked "World DB/schema check" {
      & $worldServer `
        --check-world-db `
        --config $ConfigPath `
        --db-host $DbHost `
        --db-port $DbPort `
        --db-name $DbName `
        --db-user $DbUser
    }
  }

  Write-Host "== Starting source2 world server =="
  Write-Host (Format-CommandForDisplay -Executable $worldServer -Arguments $serveArgs)
  & $worldServer @serveArgs
  $serveExitCode = $LASTEXITCODE
  if ($serveExitCode -ne 0) {
    throw "source2 world server exited with code $serveExitCode"
  }
} finally {
  $env:EQ2_DB_PASSWORD = $previousDbPassword
  $env:EQ2_WORLD_ACCOUNT = $previousWorldAccount
  $env:EQ2_WORLD_PASSWORD = $previousWorldPassword
}
