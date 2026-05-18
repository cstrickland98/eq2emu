[CmdletBinding()]
param(
  [string]$SourceDir = "E:\Games\Everquest II",
  [string]$OutputDir = "artifacts\eq2-client-sandbox",
  [string]$LoginHost = "192.168.1.41",
  [string[]]$JunctionDirs = @("paks", "music"),
  [switch]$Force,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

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

function Set-LoginHostLine {
  param(
    [string[]]$Lines,
    [string]$LoginHost
  )

  $result = [System.Collections.Generic.List[string]]::new()
  $updated = $false
  foreach ($line in $Lines) {
    if ($line -match '^\s*cl_ls_address\b') {
      if (!$updated) {
        [void]$result.Add("cl_ls_address $LoginHost")
        $updated = $true
      }
      continue
    }
    [void]$result.Add($line)
  }

  if (!$updated) {
    $result.Insert(0, "cl_ls_address $LoginHost")
  }

  return $result.ToArray()
}

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
  throw "SourceDir is required."
}
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  throw "OutputDir is required."
}
if ([string]::IsNullOrWhiteSpace($LoginHost)) {
  throw "LoginHost is required."
}
if ($LoginHost -match ':') {
  throw "LoginHost must be a host or IPv4 address without a port for the legacy EQ2 client."
}

$source = [System.IO.Path]::GetFullPath($SourceDir)
$output = Resolve-RepoPath $OutputDir

if (!(Test-Path -LiteralPath $source)) {
  throw "Missing EQ2 client source directory '$source'."
}

$required = @("EverQuest2.exe", "eq2_default.ini")
foreach ($name in $required) {
  if (!(Test-Path -LiteralPath (Join-Path $source $name))) {
    throw "Missing required EQ2 client file '$name' under '$source'."
  }
}

$junctionSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
foreach ($name in $JunctionDirs) {
  if (![string]::IsNullOrWhiteSpace($name)) {
    [void]$junctionSet.Add($name)
  }
}

if ($DryRun) {
  Write-Host "== source2 EQ2 client sandbox dry run =="
  Write-Host "Source: $source"
  Write-Host "Output: $output"
  Write-Host "Login host: $LoginHost"
  Write-Host "Planned root file copies:"
  Get-ChildItem -LiteralPath $source -File -Force | ForEach-Object {
    Write-Host "  copy $($_.Name)"
  }
  Write-Host "Planned directories:"
  Get-ChildItem -LiteralPath $source -Directory -Force | ForEach-Object {
    if ($junctionSet.Contains($_.Name)) {
      Write-Host "  junction $($_.Name) -> $($_.FullName)"
    } else {
      Write-Host "  copy $($_.Name)"
    }
  }
  Write-Host "Planned config update: cl_ls_address $LoginHost"
  return
}

if ((Test-Path -LiteralPath $output) -and !$Force) {
  throw "OutputDir '$output' already exists. Pass -Force to update it."
}

New-Item -ItemType Directory -Force -Path $output | Out-Null

$manifest = [System.Collections.Generic.List[string]]::new()
[void]$manifest.Add("SourceDir`t$source")
[void]$manifest.Add("OutputDir`t$output")
[void]$manifest.Add("LoginHost`t$LoginHost")
[void]$manifest.Add("PreparedAt`t$((Get-Date).ToString("o"))")
[void]$manifest.Add("")
[void]$manifest.Add("Kind`tName`tSource`tDestination")

Get-ChildItem -LiteralPath $source -File -Force | ForEach-Object {
  $dest = Join-Path $output $_.Name
  Copy-Item -LiteralPath $_.FullName -Destination $dest -Force
  [void]$manifest.Add(("file`t{0}`t{1}`t{2}" -f $_.Name,$_.FullName,$dest))
}

Get-ChildItem -LiteralPath $source -Directory -Force | ForEach-Object {
  $dest = Join-Path $output $_.Name
  if ($junctionSet.Contains($_.Name)) {
    if (!(Test-Path -LiteralPath $dest)) {
      New-Item -ItemType Junction -Path $dest -Target $_.FullName | Out-Null
    }
    [void]$manifest.Add(("junction`t{0}`t{1}`t{2}" -f $_.Name,$_.FullName,$dest))
    return
  }

  if (Test-Path -LiteralPath $dest) {
    Remove-Item -LiteralPath $dest -Recurse -Force
  }
  Copy-Item -LiteralPath $_.FullName -Destination $output -Recurse -Force
  [void]$manifest.Add(("directory`t{0}`t{1}`t{2}" -f $_.Name,$_.FullName,$dest))
}

$config = Join-Path $output "eq2_default.ini"
$backup = "$config.source2-backup"
if (!(Test-Path -LiteralPath $backup)) {
  Copy-Item -LiteralPath $config -Destination $backup
}
$updated = Set-LoginHostLine -Lines @(Get-Content -LiteralPath $config) -LoginHost $LoginHost
Set-Content -LiteralPath $config -Value $updated

$manifestPath = Join-Path $output "source2_sandbox_manifest.tsv"
Set-Content -LiteralPath $manifestPath -Value $manifest

Write-Host "Prepared EQ2 client sandbox: $output"
Write-Host "Set sandbox cl_ls_address $LoginHost"
Write-Host "Original sandbox config backup: $backup"
Write-Host "Sandbox manifest: $manifestPath"
