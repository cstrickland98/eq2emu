param(
  [string]$BuildDir = "build/source2",
  [string]$Config = "Debug",
  [switch]$LiveSmoke,
  [switch]$UseVcpkg,
  [string]$VcpkgRoot = $env:VCPKG_ROOT
)

$ErrorActionPreference = "Stop"

function Convert-ToCMakePath([string]$Path) {
  return ([System.IO.Path]::GetFullPath($Path) -replace "\\", "/")
}

function Resolve-VcpkgRoot([string]$RequestedRoot) {
  $candidates = @()
  if (![string]::IsNullOrWhiteSpace($RequestedRoot)) {
    $candidates += $RequestedRoot
  }

  $repoRoot = (Resolve-Path ".").Path
  $siblingRoot = Join-Path (Split-Path $repoRoot -Parent) "vcpkg"
  $candidates += $siblingRoot

  foreach ($candidate in $candidates) {
    if ([string]::IsNullOrWhiteSpace($candidate)) {
      continue
    }

    $toolchain = Join-Path $candidate "scripts/buildsystems/vcpkg.cmake"
    if (Test-Path $toolchain) {
      return (Resolve-Path $candidate).Path
    }
  }

  throw "vcpkg was requested but no vcpkg root with scripts/buildsystems/vcpkg.cmake was found. Set -VcpkgRoot or VCPKG_ROOT."
}

function Invoke-Native([string]$Command, [string[]]$Arguments) {
  & $Command @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$Command failed with exit code $LASTEXITCODE"
  }
}

$liveSmokeValue = if ($LiveSmoke.IsPresent) { "ON" } else { "OFF" }
$buildFullPath = [System.IO.Path]::GetFullPath($BuildDir)

$configureArgs = @(
  "-S", ".",
  "-B", $BuildDir,
  "-DEQ2EMU_BUILD_SOURCE2=ON",
  "-DEQ2_SOURCE2_BUILD_APPS=ON",
  "-DEQ2_SOURCE2_BUILD_TOOLS=ON",
  "-DEQ2_SOURCE2_ENABLE_LIVE_SMOKE_TESTS=$liveSmokeValue"
)

if ($UseVcpkg.IsPresent) {
  $resolvedVcpkgRoot = Resolve-VcpkgRoot $VcpkgRoot
  $toolchain = Join-Path $resolvedVcpkgRoot "scripts/buildsystems/vcpkg.cmake"
  $installedDir = Convert-ToCMakePath (Join-Path $buildFullPath "vcpkg-installed")
  $buildtreesRoot = Convert-ToCMakePath (Join-Path $buildFullPath "vcpkg-buildtrees")
  $packagesRoot = Convert-ToCMakePath (Join-Path $buildFullPath "vcpkg-packages")
  $downloadsRoot = Convert-ToCMakePath (Join-Path $resolvedVcpkgRoot "downloads")

  if ([string]::IsNullOrWhiteSpace($env:VCPKG_DEFAULT_BINARY_CACHE)) {
    $env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $buildFullPath "vcpkg-bincache"
  }
  New-Item -ItemType Directory -Force -Path $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
  New-Item -ItemType Directory -Force -Path $buildtreesRoot | Out-Null
  New-Item -ItemType Directory -Force -Path $packagesRoot | Out-Null
  New-Item -ItemType Directory -Force -Path $downloadsRoot | Out-Null

  $configureArgs += @(
    "-DCMAKE_TOOLCHAIN_FILE=$(Convert-ToCMakePath $toolchain)",
    "-DVCPKG_INSTALLED_DIR=$installedDir",
    "-DVCPKG_INSTALL_OPTIONS=--x-buildtrees-root=$buildtreesRoot;--x-packages-root=$packagesRoot;--downloads-root=$downloadsRoot"
  )
}

Invoke-Native "cmake" $configureArgs

Invoke-Native "cmake" @("--build", $BuildDir, "--config", $Config)
Invoke-Native "ctest" @("--test-dir", $BuildDir, "-C", $Config, "--output-on-failure")
