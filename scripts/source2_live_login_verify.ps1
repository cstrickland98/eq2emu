[CmdletBinding()]
param(
  [string]$BuildDir = "build\source2-vcpkg-user-verify",
  [string]$DbHost = "192.168.1.243",
  [int]$DbPort = 3306,
  [string]$LoginDbName = "eq2ls",
  [string]$WorldDbName = "eq2emu",
  [string]$DbUser = "eq2emu",
  [string]$DbPassword = "",
  [string]$LoginUsername = "testlabs",
  [string]$LoginPassword = "",
  [int]$ClientVersion = 546,
  [switch]$RunMariaDbSmoke,
  [switch]$RunServeProbe,
  [switch]$RunWorldServe,
  [int]$ExpectedWorldCount = -1,
  [int]$LoginPort = 9100,
  [int]$WorldPort = 9101,
  [string]$WorldName = "Source2 World",
  [string]$WorldAdvertisedAddress = "127.0.0.1",
  [string]$WorldServerVersion = "source2",
  [string]$WorldAccount = "",
  [string]$WorldPassword = "",
  [int]$ServeTimeoutMs = 30000
)

$ErrorActionPreference = "Stop"

function Resolve-Source2Exe {
  param(
    [string]$Name
  )

  $path = Join-Path $BuildDir "source2\apps\Debug\$Name"
  if (!(Test-Path $path)) {
    throw "Missing $Name at '$path'. Build first with scripts\source2_ci.ps1 -UseVcpkg -LiveSmoke -BuildDir $BuildDir"
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

function Get-LocalIPv4Candidates {
  $addresses = @()
  try {
    $addresses = Get-NetIPConfiguration -ErrorAction SilentlyContinue |
      ForEach-Object { $_.IPv4Address.IPAddress } |
      Where-Object { $_ -and $_ -notlike "127.*" } |
      Sort-Object -Unique
  } catch {
    $addresses = @()
  }

  if ($addresses.Count -eq 0) {
    try {
      $addresses = ipconfig |
        Select-String -Pattern "IPv4 Address.*:\s*([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)" |
        ForEach-Object { $_.Matches[0].Groups[1].Value } |
        Where-Object { $_ -and $_ -notlike "127.*" } |
        Sort-Object -Unique
    } catch {
      $addresses = @()
    }
  }

  return @($addresses)
}

function Test-TcpEndpoint {
  param(
    [string]$TargetHost,
    [int]$TargetPort,
    [int]$TimeoutMs = 3000
  )

  $result = [ordered]@{
    Succeeded = $false
    Kind = "ConnectFailed"
    Message = ""
  }

  $client = $null
  try {
    $client = [System.Net.Sockets.TcpClient]::new()
    $async = $client.BeginConnect($TargetHost, $TargetPort, $null, $null)
    if (!$async.AsyncWaitHandle.WaitOne($TimeoutMs, $false)) {
      $result.Kind = "Timeout"
      $result.Message = "TCP connect timed out after $TimeoutMs ms."
      return [pscustomobject]$result
    }

    try {
      $client.EndConnect($async)
      $result.Succeeded = $true
      $result.Kind = "Connected"
      $result.Message = "Connected."
    } catch {
      $message = $_.Exception.Message
      if ($_.Exception.InnerException -and ![string]::IsNullOrWhiteSpace($_.Exception.InnerException.Message)) {
        $message = $_.Exception.InnerException.Message
      }

      $result.Message = $message
      if ($message -match "forbidden|access permissions|permission denied") {
        $result.Kind = "LocalPolicy"
      } elseif ($message -match "actively refused|refused") {
        $result.Kind = "Refused"
      }
    }
  } catch {
    $result.Message = $_.Exception.Message
    if ($_.Exception.InnerException -and ![string]::IsNullOrWhiteSpace($_.Exception.InnerException.Message)) {
      $result.Message = $_.Exception.InnerException.Message
    }
  } finally {
    if ($client) {
      $client.Close()
    }
  }

  return [pscustomobject]$result
}

function Test-CodexSandboxOutboundBlock {
  try {
    $output = & netsh advfirewall firewall show rule name=codex_sandbox_offline_block_outbound verbose 2>$null
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

function ConvertTo-StartProcessArguments {
  param(
    [string[]]$Arguments
  )

  return @($Arguments | ForEach-Object {
    if ($null -eq $_) {
      return '""'
    }

    $value = [string]$_
    if ($value.Length -eq 0) {
      return '""'
    }

    if ($value -match '[\s"]') {
      return '"' + ($value -replace '"', '\"') + '"'
    }

    return $value
  })
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
  throw "$ParamName or $EnvName is required after MariaDB TCP reachability succeeds."
}

$loginServer = Resolve-Source2Exe "eq2_login_server.exe"
$worldServer = Resolve-Source2Exe "eq2_world_server.exe"

if ($RunServeProbe -and $ExpectedWorldCount -gt 0 -and !$RunWorldServe) {
  Write-Warning "ExpectedWorldCount > 0 without -RunWorldServe requires an external world server to register with this verifier's temporary login server. Use -ExpectedWorldCount 0 for a login-only gate."
}

Write-Host "== MariaDB TCP reachability =="
$tcp = Test-NetConnection -ComputerName $DbHost -Port $DbPort
if (!$tcp.TcpTestSucceeded) {
  $tcpDetail = Test-TcpEndpoint -TargetHost $DbHost -TargetPort $DbPort
  if (!$tcpDetail.Succeeded -and ![string]::IsNullOrWhiteSpace($tcpDetail.Message)) {
    Write-Host "TCP failure detail: [$($tcpDetail.Kind)] $($tcpDetail.Message)"
  }
  $localAddresses = @(Get-LocalIPv4Candidates)
  if ($localAddresses.Count -gt 0) {
    Write-Host "Local IPv4 candidates for MariaDB firewall/grants: $($localAddresses -join ', ')"
  }
  if ($tcpDetail.Kind -eq "LocalPolicy") {
    if (Test-CodexSandboxOutboundBlock) {
      Write-Host "Detected Codex sandbox offline outbound firewall rule; rerun from a normal PowerShell prompt or a network-enabled Codex session."
    }
    Write-Host "Source2 host checks:"
    Write-Host "  1. Confirm this shell/IDE is allowed to open outbound TCP sockets to $DbHost`:$DbPort."
    Write-Host "  2. Check local outbound firewall or security policy for TCP $DbPort."
    Write-Host "  3. Retest from a normal PowerShell prompt on the source2 host if this environment is sandboxed."
  }
  Write-Host "Next checks on the MariaDB host:"
  Write-Host "  1. Confirm MariaDB is listening on $DbPort for the LAN address or 0.0.0.0, not only 127.0.0.1."
  Write-Host "  2. Allow inbound TCP $DbPort from this source2 host in the MariaDB host firewall."
  Write-Host "  3. Confirm '$DbUser' has grants from this host for '$LoginDbName' and '$WorldDbName'."
  throw "Cannot reach $DbHost`:$DbPort from this host. Fix source2 outbound policy and/or MariaDB bind/firewall/grants before rerunning."
}

$DbPassword = Resolve-Secret -Value $DbPassword -EnvName "EQ2_DB_PASSWORD" -ParamName "-DbPassword"
$LoginPassword = Resolve-Secret -Value $LoginPassword -EnvName "EQ2_LOGIN_PASSWORD" -ParamName "-LoginPassword"
if ($RunWorldServe) {
  $WorldAccount = Resolve-Secret -Value $WorldAccount -EnvName "EQ2_WORLD_ACCOUNT" -ParamName "-WorldAccount"
  $WorldPassword = Resolve-Secret -Value $WorldPassword -EnvName "EQ2_WORLD_PASSWORD" -ParamName "-WorldPassword"
}

$previousDbPassword = $env:EQ2_DB_PASSWORD
$previousLoginUsername = $env:EQ2_LOGIN_USERNAME
$previousLoginPassword = $env:EQ2_LOGIN_PASSWORD
$previousWorldAccount = $env:EQ2_WORLD_ACCOUNT
$previousWorldPassword = $env:EQ2_WORLD_PASSWORD
$env:EQ2_DB_PASSWORD = $DbPassword
$env:EQ2_LOGIN_USERNAME = $LoginUsername
$env:EQ2_LOGIN_PASSWORD = $LoginPassword
if ($RunWorldServe) {
  $env:EQ2_WORLD_ACCOUNT = $WorldAccount
  $env:EQ2_WORLD_PASSWORD = $WorldPassword
}

try {
Invoke-Checked "Login DB/account/opcode check" {
  & $loginServer `
    --check-login-db `
    --db-host $DbHost `
    --db-port $DbPort `
    --db-name $LoginDbName `
    --db-user $DbUser `
    --client-version $ClientVersion
}

Invoke-Checked "World DB/schema check" {
  & $worldServer `
    --check-world-db `
    --db-host $DbHost `
    --db-port $DbPort `
    --db-name $WorldDbName `
    --db-user $DbUser
}

if ($RunMariaDbSmoke) {
  Invoke-Checked "MariaDB-backed login smoke" {
    & $loginServer `
      --smoke-login-mariadb `
      --db-host $DbHost `
      --db-port $DbPort `
      --db-name $LoginDbName `
      --db-user $DbUser `
      --client-version $ClientVersion
  }
}

if ($RunServeProbe) {
  Write-Host "== Login serve/probe =="
  $stdout = Join-Path ([System.IO.Path]::GetTempPath()) "eq2_login_server_serve_stdout.log"
  $stderr = Join-Path ([System.IO.Path]::GetTempPath()) "eq2_login_server_serve_stderr.log"
  $worldStdout = Join-Path ([System.IO.Path]::GetTempPath()) "eq2_world_server_serve_stdout.log"
  $worldStderr = Join-Path ([System.IO.Path]::GetTempPath()) "eq2_world_server_serve_stderr.log"
  Remove-Item -LiteralPath $stdout, $stderr, $worldStdout, $worldStderr -ErrorAction SilentlyContinue

  $serveArgs = @(
    "--serve",
    "--run-for-ms", "$ServeTimeoutMs",
    "--login-address", "127.0.0.1",
    "--login-port", "$LoginPort",
    "--login-opcode-source", "database",
    "--db-host", $DbHost,
    "--db-port", "$DbPort",
    "--db-name", $LoginDbName,
    "--db-user", $DbUser,
    "--client-version", "$ClientVersion"
  )

  $worldProcess = $null
  $env:EQ2_LOGIN_USERNAME = $null
  $env:EQ2_LOGIN_PASSWORD = $null
  $env:EQ2_WORLD_ACCOUNT = $null
  $env:EQ2_WORLD_PASSWORD = $null
  $process = Start-Process -FilePath $loginServer `
    -ArgumentList (ConvertTo-StartProcessArguments $serveArgs) `
    -PassThru `
    -NoNewWindow `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr
  try {
    $started = $false
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
      if ($process.HasExited) {
        break
      }
      $probe = Test-NetConnection -ComputerName "127.0.0.1" -Port $LoginPort -InformationLevel Quiet -WarningAction SilentlyContinue
      if ($probe) {
        $started = $true
        break
      }
      Start-Sleep -Milliseconds 100
    }

    if (!$started) {
      $serveOut = if (Test-Path $stdout) { Get-Content -LiteralPath $stdout -Raw } else { "" }
      $serveErr = if (Test-Path $stderr) { Get-Content -LiteralPath $stderr -Raw } else { "" }
      throw "Login serve did not open TCP $LoginPort. stdout: $serveOut stderr: $serveErr"
    }

    if ($RunWorldServe) {
      $env:EQ2_WORLD_ACCOUNT = $WorldAccount
      $env:EQ2_WORLD_PASSWORD = $WorldPassword
      $worldArgs = @(
        "--serve",
        "--run-for-ms", "$ServeTimeoutMs",
        "--world-address", "127.0.0.1",
        "--world-port", "$WorldPort",
        "--world-advertised-address", $WorldAdvertisedAddress,
        "--world-name", $WorldName,
        "--world-server-version", $WorldServerVersion,
        "--login-address", "127.0.0.1",
        "--login-port", "$LoginPort",
        "--db-host", $DbHost,
        "--db-port", "$DbPort",
        "--db-name", $WorldDbName,
        "--db-user", $DbUser
      )

      $worldProcess = Start-Process -FilePath $worldServer `
        -ArgumentList (ConvertTo-StartProcessArguments $worldArgs) `
        -PassThru `
        -NoNewWindow `
        -RedirectStandardOutput $worldStdout `
        -RedirectStandardError $worldStderr

      $env:EQ2_WORLD_ACCOUNT = $null
      $env:EQ2_WORLD_PASSWORD = $null

      $worldStarted = $false
      for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        if ($worldProcess.HasExited) {
          break
        }
        $worldProbe = Test-NetConnection -ComputerName "127.0.0.1" -Port $WorldPort -InformationLevel Quiet -WarningAction SilentlyContinue
        if ($worldProbe) {
          $worldStarted = $true
          break
        }
        Start-Sleep -Milliseconds 100
      }

      if (!$worldStarted) {
        $worldOut = if (Test-Path $worldStdout) { Get-Content -LiteralPath $worldStdout -Raw } else { "" }
        $worldErr = if (Test-Path $worldStderr) { Get-Content -LiteralPath $worldStderr -Raw } else { "" }
        throw "World serve did not open TCP $WorldPort. stdout: $worldOut stderr: $worldErr"
      }

      Start-Sleep -Milliseconds 500
    }

    Invoke-Checked "Probe running login server" {
      $env:EQ2_LOGIN_USERNAME = $LoginUsername
      $env:EQ2_LOGIN_PASSWORD = $LoginPassword
      $probeArgs = @(
        "--probe-login",
        "--connect-host", "127.0.0.1",
        "--connect-port", "$LoginPort",
        "--login-opcode-source", "database",
        "--db-host", $DbHost,
        "--db-port", "$DbPort",
        "--db-name", $LoginDbName,
        "--db-user", $DbUser,
        "--client-version", "$ClientVersion"
      )
      if ($ExpectedWorldCount -ge 0) {
        $probeArgs += @("--expect-world-count", "$ExpectedWorldCount")
      }
      & $loginServer @probeArgs
    }
  } finally {
    if ($worldProcess -and !$worldProcess.HasExited) {
      Stop-Process -Id $worldProcess.Id -Force
      $worldProcess.WaitForExit()
    }
    if ($process -and !$process.HasExited) {
      Stop-Process -Id $process.Id -Force
      $process.WaitForExit()
    }
  }
}

Write-Host "source2 live login verification passed."
} finally {
  $env:EQ2_DB_PASSWORD = $previousDbPassword
  $env:EQ2_LOGIN_USERNAME = $previousLoginUsername
  $env:EQ2_LOGIN_PASSWORD = $previousLoginPassword
  $env:EQ2_WORLD_ACCOUNT = $previousWorldAccount
  $env:EQ2_WORLD_PASSWORD = $previousWorldPassword
}
