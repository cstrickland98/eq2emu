[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [ValidateSet("Login", "World", "Both")]
  [string]$Kind = "Both",
  [string]$OutputDir = "artifacts\source2-deploy-config",
  [string]$LoginOutputPath = "",
  [string]$WorldOutputPath = "",
  [switch]$Force,
  [switch]$DryRun
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath {
  param(
    [string]$RelativePath
  )

  $repoRoot = Split-Path -Parent $PSScriptRoot
  return Join-Path $repoRoot $RelativePath
}

function Resolve-OutputPath {
  param(
    [string]$ExplicitPath,
    [string]$DefaultName
  )

  if (![string]::IsNullOrWhiteSpace($ExplicitPath)) {
    return $ExplicitPath
  }
  return Join-Path $OutputDir $DefaultName
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

function Copy-DeployConfig {
  param(
    [string]$SourcePath,
    [string]$DestinationPath,
    [string]$Label
  )

  if (!(Test-Path -LiteralPath $SourcePath)) {
    throw "Missing $Label config template '$SourcePath'."
  }

  if ($DryRun) {
    Write-Host "$Label config:"
    Write-Host "  source: $SourcePath"
    Write-Host "  destination: $DestinationPath"
    Write-Host "  command: Copy-Item -LiteralPath $(Format-ArgumentForDisplay $SourcePath) -Destination $(Format-ArgumentForDisplay $DestinationPath)"
    return
  }

  if ((Test-Path -LiteralPath $DestinationPath) -and !$Force) {
    throw "$Label config '$DestinationPath' already exists. Use -Force to overwrite."
  }

  $destinationDir = Split-Path -Parent $DestinationPath
  if (![string]::IsNullOrWhiteSpace($destinationDir)) {
    New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
  }

  if ($PSCmdlet.ShouldProcess($DestinationPath, "Create $Label deploy config")) {
    Copy-Item -LiteralPath $SourcePath -Destination $DestinationPath -Force:$Force
    Write-Host "Created $Label deploy config: $DestinationPath"
  }
}

$loginSource = Resolve-RepoPath "source2\config\login_server.eq2emu-target.ini.example"
$worldSource = Resolve-RepoPath "source2\config\world_server.eq2emu-target.ini.example"
$loginDestination = Resolve-OutputPath -ExplicitPath $LoginOutputPath -DefaultName "login_server.eq2emu-target.ini"
$worldDestination = Resolve-OutputPath -ExplicitPath $WorldOutputPath -DefaultName "world_server.eq2emu-target.ini"

Write-Host "== Source2 deploy config preparation =="
Write-Host "Kind: $Kind"
Write-Host "OutputDir: $OutputDir"
Write-Host "Secrets: placeholders are preserved; edit deploy configs outside source control."

if ($Kind -eq "Login" -or $Kind -eq "Both") {
  Copy-DeployConfig -SourcePath $loginSource -DestinationPath $loginDestination -Label "Login"
}

if ($Kind -eq "World" -or $Kind -eq "Both") {
  Copy-DeployConfig -SourcePath $worldSource -DestinationPath $worldDestination -Label "World"
}
