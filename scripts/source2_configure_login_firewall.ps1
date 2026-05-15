[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [int]$LoginPort = 9100,
  [string]$RemoteAddress = "Any",
  [string]$RulePrefix = "Source2 Login",
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

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

function Format-FirewallCommandForDisplay {
  param(
    [string]$DisplayName,
    [string]$Protocol
  )

  $parts = @(
    "New-NetFirewallRule",
    "-DisplayName", (Format-ArgumentForDisplay $DisplayName),
    "-Direction", "Inbound",
    "-Action", "Allow",
    "-Protocol", $Protocol,
    "-LocalPort", "$LoginPort",
    "-RemoteAddress", (Format-ArgumentForDisplay $RemoteAddress)
  )
  return $parts -join " "
}

function Test-IsAdministrator {
  $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
  $principal = [Security.Principal.WindowsPrincipal]::new($identity)
  return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Ensure-FirewallRule {
  param(
    [string]$DisplayName,
    [string]$Protocol
  )

  if ($DryRun) {
    Write-Host (Format-FirewallCommandForDisplay -DisplayName $DisplayName -Protocol $Protocol)
    return
  }

  $existing = Get-NetFirewallRule -DisplayName $DisplayName -ErrorAction SilentlyContinue
  if ($existing) {
    Write-Host "Firewall rule already exists: $DisplayName"
    return
  }

  if ($PSCmdlet.ShouldProcess($DisplayName, "Create inbound $Protocol firewall rule")) {
    New-NetFirewallRule `
      -DisplayName $DisplayName `
      -Direction Inbound `
      -Action Allow `
      -Protocol $Protocol `
      -LocalPort $LoginPort `
      -RemoteAddress $RemoteAddress | Out-Null
    Write-Host "Created firewall rule: $DisplayName"
  }
}

if ($LoginPort -lt 1 -or $LoginPort -gt 65535) {
  throw "LoginPort must be an integer from 1 to 65535."
}

$udpRuleName = "$RulePrefix UDP $LoginPort"
$tcpRuleName = "$RulePrefix TCP $LoginPort"

Write-Host "== Source2 login firewall setup =="
Write-Host "Login port: $LoginPort"
Write-Host "Remote address: $RemoteAddress"
Write-Host "Rules:"
Write-Host "  $udpRuleName"
Write-Host "  $tcpRuleName"

if ($DryRun) {
  Write-Host "== Dry run commands =="
} elseif (!(Test-IsAdministrator)) {
  throw "Run this script from an elevated PowerShell prompt, or use -DryRun to print the commands."
}

Ensure-FirewallRule -DisplayName $udpRuleName -Protocol "UDP"
Ensure-FirewallRule -DisplayName $tcpRuleName -Protocol "TCP"
