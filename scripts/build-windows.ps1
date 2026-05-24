param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

    [string]$VcpkgRoot = $env:VCPKG_ROOT,

    [switch]$Clean,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Resolve-VcpkgRoot {
    param([string]$Candidate)

    if ($Candidate -and (Test-Path (Join-Path $Candidate "scripts\buildsystems\vcpkg.cmake"))) {
        return (Resolve-Path $Candidate).Path
    }

    $Sibling = Join-Path $RepoRoot "..\vcpkg"
    if (Test-Path (Join-Path $Sibling "scripts\buildsystems\vcpkg.cmake")) {
        return (Resolve-Path $Sibling).Path
    }

    throw "Set VCPKG_ROOT to a vcpkg checkout before running this script."
}

function Import-VsDevEnvironment {
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path $VsWhere)) {
        throw "vswhere.exe was not found. Install Visual Studio 2022 with the C++ workload."
    }

    $InstallPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$InstallPath) {
        throw "Visual Studio C++ tools were not found."
    }

    $VsDevCmd = Join-Path $InstallPath "Common7\Tools\VsDevCmd.bat"
    if (!(Test-Path $VsDevCmd)) {
        throw "VsDevCmd.bat was not found under $InstallPath."
    }

    $EnvLines = cmd /s /c "`"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && set"
    foreach ($Line in $EnvLines) {
        $Parts = $Line -split "=", 2
        if ($Parts.Count -eq 2) {
            Set-Item -Path "env:$($Parts[0])" -Value $Parts[1]
        }
    }
}

function Find-Ninja {
    $Downloads = Join-Path $script:ResolvedVcpkgRoot "downloads\tools"
    if (Test-Path $Downloads) {
        $Bundled = Get-ChildItem -Path $Downloads -Filter ninja.exe -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($Bundled) {
            return $Bundled.FullName
        }
    }

    $Ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue
    if ($Ninja) {
        return $Ninja.Source
    }

    throw "ninja.exe was not found. Install Ninja or place it on PATH."
}

function Invoke-Native {
    $Command = $args[0]
    $CommandArgs = @()
    if ($args.Count -gt 1) {
        $CommandArgs = $args[1..($args.Count - 1)]
    }

    & $Command @CommandArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $($args -join ' ')"
    }
}

$script:ResolvedVcpkgRoot = Resolve-VcpkgRoot $VcpkgRoot
$env:VCPKG_ROOT = $script:ResolvedVcpkgRoot

Import-VsDevEnvironment
$env:VCPKG_ROOT = $script:ResolvedVcpkgRoot

$NinjaPath = Find-Ninja
$env:PATH = "$(Split-Path $NinjaPath -Parent);$env:PATH"
$env:VCPKG_FORCE_SYSTEM_BINARIES = "1"

$BuildDir = Join-Path $RepoRoot "build\windows-msvc"
if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}

$VcpkgInstallOptions = @(
    "--x-buildtrees-root=$((Join-Path $RepoRoot 'build\vcpkg-buildtrees'))",
    "--downloads-root=$((Join-Path $RepoRoot 'build\vcpkg-downloads'))",
    "--x-packages-root=$((Join-Path $RepoRoot 'build\vcpkg-packages'))",
    "--binarysource=clear"
)

if ($Python = Get-Command python -ErrorAction SilentlyContinue) {
    $Downloader = Join-Path $PSScriptRoot "vcpkg-download.py"
    $env:X_VCPKG_ASSET_SOURCES = "clear;x-script,`"$($Python.Source)`" `"$Downloader`" {url} {dst}"
}

Invoke-Native cmake --preset windows-msvc-vcpkg `
    -DCMAKE_BUILD_TYPE="$Configuration" `
    -DCMAKE_MAKE_PROGRAM="$NinjaPath" `
    "-DVCPKG_INSTALL_OPTIONS=$($VcpkgInstallOptions -join ';')"

if (!$SkipBuild) {
    Invoke-Native cmake --build --preset windows-msvc-vcpkg --config $Configuration
}
