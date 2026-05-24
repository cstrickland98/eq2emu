param(
    [ValidateSet("prepare", "login", "world", "both")]
    [string]$Mode = "prepare",

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",

    [string]$BuildDir = (Join-Path $PSScriptRoot "..\build\windows-msvc"),
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$ContentDir = "",
    [switch]$CheckPorts,
    [int]$PortCheckDelaySeconds = 45
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ServerDir = Join-Path $RepoRoot "server"
$BinDir = Join-Path (Resolve-Path $BuildDir) "bin"

function Copy-IfMissing {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (!(Test-Path $Destination)) {
        Copy-Item -LiteralPath $Source -Destination $Destination
    }
}

function Resolve-VcpkgRoot {
    param([string]$Candidate)

    if ($Candidate -and (Test-Path (Join-Path $Candidate "scripts\buildsystems\vcpkg.cmake"))) {
        return (Resolve-Path $Candidate).Path
    }

    $Sibling = Join-Path $RepoRoot "..\vcpkg"
    if (Test-Path (Join-Path $Sibling "scripts\buildsystems\vcpkg.cmake")) {
        return (Resolve-Path $Sibling).Path
    }

    return $null
}

function Copy-RuntimeDlls {
    Copy-Item -Path (Join-Path $BinDir "*.dll") -Destination $ServerDir -ErrorAction SilentlyContinue

    $ResolvedVcpkgRoot = Resolve-VcpkgRoot $VcpkgRoot
    $TripletRoot = Join-Path (Resolve-Path $BuildDir) "vcpkg_installed\x64-windows"
    if (!(Test-Path $TripletRoot)) {
        $TripletRoot = Join-Path $RepoRoot "vcpkg_installed\x64-windows"
    }
    if (!(Test-Path $TripletRoot) -and $ResolvedVcpkgRoot) {
        $TripletRoot = Join-Path $ResolvedVcpkgRoot "installed\x64-windows"
    }

    foreach ($DllDir in @((Join-Path $TripletRoot "bin"), (Join-Path $TripletRoot "debug\bin"))) {
        if (Test-Path $DllDir) {
            Copy-Item -Path (Join-Path $DllDir "*.dll") -Destination $ServerDir -ErrorAction SilentlyContinue
        }
    }

    $PluginTarget = Join-Path $ServerDir "plugins\libmariadb"
    New-Item -ItemType Directory -Path $PluginTarget -Force | Out-Null
    foreach ($PluginDir in @((Join-Path $TripletRoot "plugins\libmariadb"), (Join-Path $TripletRoot "debug\plugins\libmariadb"))) {
        if (Test-Path $PluginDir) {
            Copy-Item -Path (Join-Path $PluginDir "*.dll") -Destination $ServerDir -ErrorAction SilentlyContinue
            Copy-Item -Path (Join-Path $PluginDir "*.dll") -Destination $PluginTarget -ErrorAction SilentlyContinue
        }
    }
}

function Resolve-ContentDir {
    if ($ContentDir -and (Test-Path $ContentDir)) {
        return (Resolve-Path $ContentDir).Path
    }

    $Sibling = Join-Path $RepoRoot "..\eq2emu-content"
    if (Test-Path $Sibling) {
        return (Resolve-Path $Sibling).Path
    }

    return $null
}

function Link-ContentDirs {
    $ResolvedContentDir = Resolve-ContentDir
    if (!$ResolvedContentDir) {
        return
    }

    foreach ($Name in @("ItemScripts", "PlayerScripts", "Quests", "RegionScripts", "SpawnScripts", "Spells", "ZoneScripts")) {
        $Source = Join-Path $ResolvedContentDir $Name
        $Destination = Join-Path $ServerDir $Name
        if ((Test-Path $Source) -and !(Test-Path $Destination)) {
            New-Item -ItemType Junction -Path $Destination -Target $Source | Out-Null
        }
    }
}

function Update-LocalConfigDefaults {
    $ConfigPath = Join-Path $ServerDir "server_config.json"
    if (!(Test-Path $ConfigPath)) {
        return
    }

    $Config = Get-Content -Raw -Path $ConfigPath | ConvertFrom-Json
    $Changed = $false
    foreach ($Property in @("loginserver", "worldaddress", "internalworldaddress")) {
        if ($Config.LoginServer.$Property -eq "ENTERIP") {
            $Config.LoginServer.$Property = "127.0.0.1"
            $Changed = $true
        }
    }

    if ($Changed) {
        $Config | ConvertTo-Json -Depth 8 | Set-Content -Path $ConfigPath -Encoding ASCII
    }
}

function Assert-Binary {
    param([string]$Name)

    $Path = Join-Path $BinDir $Name
    if (!(Test-Path $Path)) {
        throw "$Name was not found in $BinDir. Run scripts\build-windows.ps1 first."
    }

    Copy-Item -LiteralPath $Path -Destination (Join-Path $ServerDir $Name) -Force
}

function Test-ServerPorts {
    $Ports = @(9001, 9100)
    foreach ($Port in $Ports) {
        $Lines = @(netstat -ano | Select-String -Pattern "^\s*(TCP|UDP)\s+\S+:$Port\s")
        if ($Lines.Count -gt 0) {
            Write-Host "Port $Port is bound."
            $Lines | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
        }
        else {
            Write-Warning "Port $Port is not bound."
        }
    }
}

Assert-Binary "login.exe"
Assert-Binary "eq2world.exe"
Copy-RuntimeDlls

Copy-IfMissing (Join-Path $ServerDir "login_db.ini.example") (Join-Path $ServerDir "login_db.ini")
Copy-IfMissing (Join-Path $ServerDir "world_db.ini.example") (Join-Path $ServerDir "world_db.ini")
Copy-IfMissing (Join-Path $ServerDir "server_config.json.example") (Join-Path $ServerDir "server_config.json")
Copy-IfMissing (Join-Path $ServerDir "log_config.xml.example") (Join-Path $ServerDir "log_config.xml")
Link-ContentDirs
Update-LocalConfigDefaults

if ($Mode -in @("login", "both")) {
    Start-Process -FilePath (Join-Path $ServerDir "login.exe") -WorkingDirectory $ServerDir -WindowStyle Hidden
}

if ($Mode -in @("world", "both")) {
    Start-Process -FilePath (Join-Path $ServerDir "eq2world.exe") -WorkingDirectory $ServerDir -WindowStyle Hidden
}

if ($CheckPorts) {
    Start-Sleep -Seconds $PortCheckDelaySeconds
    Test-ServerPorts
}
