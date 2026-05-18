[CmdletBinding()]
param(
  [string]$ClientExePath = "E:\Games\Everquest II\EverQuest2.exe",
  [string]$ClientDir = "",
  [string[]]$ClientArguments = @(),
  [int]$WaitSeconds = 10,
  [switch]$KeepClientRunning
)

$ErrorActionPreference = "Stop"

function Resolve-ClientPath {
  param(
    [string]$Path
  )

  if ([System.IO.Path]::IsPathRooted($Path)) {
    return [System.IO.Path]::GetFullPath($Path)
  }
  return [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $Path))
}

function Read-WindowText {
  param(
    [IntPtr]$WindowHandle
  )

  if ($WindowHandle -eq [IntPtr]::Zero) {
    return ""
  }

  Add-Type -AssemblyName UIAutomationClient
  Add-Type -AssemblyName UIAutomationTypes
  $root = [System.Windows.Automation.AutomationElement]::FromHandle($WindowHandle)
  if ($null -eq $root) {
    return ""
  }

  $parts = [System.Collections.Generic.List[string]]::new()
  $walker = [System.Windows.Automation.TreeWalker]::ControlViewWalker
  $queue = [System.Collections.Queue]::new()
  $queue.Enqueue($root)
  $seen = 0
  while ($queue.Count -gt 0 -and $seen -lt 200) {
    $element = $queue.Dequeue()
    $seen += 1
    if (![string]::IsNullOrWhiteSpace($element.Current.Name)) {
      [void]$parts.Add($element.Current.Name)
    }
    $child = $walker.GetFirstChild($element)
    while ($null -ne $child) {
      $queue.Enqueue($child)
      $child = $walker.GetNextSibling($child)
    }
  }

  return [string]::Join([Environment]::NewLine, $parts)
}

if ($WaitSeconds -lt 1) {
  throw "WaitSeconds must be at least 1."
}

$clientPath = Resolve-ClientPath $ClientExePath
if (!(Test-Path -LiteralPath $clientPath)) {
  throw "Missing EQ2 client executable '$clientPath'."
}

if ([string]::IsNullOrWhiteSpace($ClientDir)) {
  $ClientDir = Split-Path -Parent $clientPath
}
$clientDirPath = Resolve-ClientPath $ClientDir

$process = $null
try {
  $startParams = @{
    FilePath = $clientPath
    WorkingDirectory = $clientDirPath
    PassThru = $true
  }
  if ($ClientArguments.Count -gt 0) {
    $startParams.ArgumentList = $ClientArguments
  }

  $process = Start-Process @startParams
  Start-Sleep -Seconds $WaitSeconds
  $process.Refresh()

  $windowTitle = if ($process.HasExited) { "" } else { $process.MainWindowTitle }
  $windowText = if ($process.HasExited) { "" } else { Read-WindowText $process.MainWindowHandle }

  Write-Host "EQ2 client DirectX/window diagnostic"
  Write-Host "Client: $clientPath"
  Write-Host "ProcessId: $($process.Id)"
  Write-Host "Exited: $($process.HasExited)"
  Write-Host "WindowTitle: $windowTitle"
  if (![string]::IsNullOrWhiteSpace($windowText)) {
    Write-Host "WindowText:"
    Write-Host $windowText
  }

  if ($process.HasExited) {
    Write-Host "Client exited before a usable window was observed."
    exit 1
  }
  if ($windowTitle -eq "Fatal Error" -or $windowText -match "D3DERR_NOTAVAILABLE|DirectX Error|unrecoverable error") {
    Write-Host "Client DirectX check failed."
    exit 1
  }

  Write-Host "Client DirectX check passed."
} finally {
  if (!$KeepClientRunning -and $process -and !$process.HasExited) {
    Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
  }
}
