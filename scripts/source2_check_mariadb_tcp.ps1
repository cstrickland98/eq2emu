[CmdletBinding()]
param(
  [string]$DbHost = "192.168.1.243",
  [int]$DbPort = 3306,
  [int]$TimeoutMs = 3000
)

$ErrorActionPreference = "Stop"

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
    [int]$ConnectTimeoutMs = 3000
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
    if (!$async.AsyncWaitHandle.WaitOne($ConnectTimeoutMs, $false)) {
      $result.Kind = "Timeout"
      $result.Message = "TCP connect timed out after $ConnectTimeoutMs ms."
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

Write-Host "== Source2 MariaDB TCP check =="
Write-Host "Target: $DbHost`:$DbPort"

$net = Test-NetConnection -ComputerName $DbHost -Port $DbPort -WarningAction SilentlyContinue
$net | Format-List ComputerName,RemoteAddress,RemotePort,PingSucceeded,TcpTestSucceeded

$detail = Test-TcpEndpoint -TargetHost $DbHost -TargetPort $DbPort -ConnectTimeoutMs $TimeoutMs
Write-Host "TCP detail: [$($detail.Kind)] $($detail.Message)"

$localAddresses = @(Get-LocalIPv4Candidates)
if ($localAddresses.Count -gt 0) {
  Write-Host "Local IPv4 candidates for MariaDB firewall/grants: $($localAddresses -join ', ')"
}

if ($net.TcpTestSucceeded -and $detail.Succeeded) {
  Write-Host "MariaDB TCP endpoint is reachable from this host."
  exit 0
}

if ($detail.Kind -eq "LocalPolicy") {
  if (Test-CodexSandboxOutboundBlock) {
    Write-Host "Detected Codex sandbox offline outbound firewall rule; rerun from a normal PowerShell prompt or a network-enabled Codex session."
  }
  Write-Host "Next source2 host check: run this script from a normal, non-sandboxed PowerShell prompt and inspect local endpoint/security policy for outbound TCP $DbPort."
} elseif ($detail.Kind -eq "Refused") {
  Write-Host "Next MariaDB host check: confirm the MariaDB service is listening on $DbHost`:$DbPort and not only on 127.0.0.1."
} elseif ($detail.Kind -eq "Timeout") {
  Write-Host "Next network check: confirm the MariaDB host firewall allows inbound TCP $DbPort from this source2 host."
} else {
  Write-Host "Next checks: confirm source2 outbound policy, MariaDB bind-address, MariaDB host firewall, and MariaDB grants."
}

exit 1
