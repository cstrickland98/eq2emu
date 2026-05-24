param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"

function Test-FileContains {
    param(
        [string]$RelativePath,
        [string]$Pattern,
        [string]$Name
    )

    $path = Join-Path $RepoRoot $RelativePath
    if (!(Test-Path -LiteralPath $path)) {
        return [pscustomobject]@{ Name = $Name; Passed = $false; Detail = "Missing $RelativePath" }
    }

    $content = Get-Content -Raw -LiteralPath $path
    $passed = [regex]::IsMatch($content, $Pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    return [pscustomobject]@{ Name = $Name; Passed = $passed; Detail = $RelativePath }
}

$checks = @(
    @{ Name = "dev mode command macro"; Path = "source\WorldServer\Commands\Commands.h"; Pattern = "COMMAND_DEVMODE" },
    @{ Name = "spawn preview command macro"; Path = "source\WorldServer\Commands\Commands.h"; Pattern = "COMMAND_SPAWNPREVIEW" },
    @{ Name = "spawn save command macro"; Path = "source\WorldServer\Commands\Commands.h"; Pattern = "COMMAND_SPAWNSAVE" },
    @{ Name = "spawn clone command macro"; Path = "source\WorldServer\Commands\Commands.h"; Pattern = "COMMAND_SPAWNCLONE" },
    @{ Name = "spawn delete command macro"; Path = "source\WorldServer\Commands\Commands.h"; Pattern = "COMMAND_SPAWNDELETE" },
    @{ Name = "dev mode handler"; Path = "source\WorldServer\Commands\Commands.cpp"; Pattern = "Command_DevMode" },
    @{ Name = "preview handler"; Path = "source\WorldServer\Commands\Commands.cpp"; Pattern = "Command_SpawnPreview" },
    @{ Name = "save handler"; Path = "source\WorldServer\Commands\Commands.cpp"; Pattern = "Command_SpawnSave" },
    @{ Name = "clone handler"; Path = "source\WorldServer\Commands\Commands.cpp"; Pattern = "Command_SpawnClone" },
    @{ Name = "delete handler"; Path = "source\WorldServer\Commands\Commands.cpp"; Pattern = "Command_SpawnDelete" },
    @{ Name = "GM status guard"; Path = "source\WorldServer\Commands\Commands.cpp"; Pattern = "GetAdminStatus\(\) < 100" },
    @{ Name = "fallback command registration"; Path = "source\WorldServer\WorldDatabase.cpp"; Pattern = 'EnsureRemoteCommand\("spawnpreview"' },
    @{ Name = "preview state on client"; Path = "source\WorldServer\client.h"; Pattern = "devPlacementPreviewSpawn" },
    @{ Name = "preview save uses existing DB APIs"; Path = "source\WorldServer\client.cpp"; Pattern = "SaveDevPlacementPreview.*SaveSpawnInfo.*SaveSpawnEntry" },
    @{ Name = "move-object preview branch"; Path = "source\WorldServer\client.cpp"; Pattern = "was_dev_preview.*Preview position updated" },
    @{ Name = "placement-level delete API"; Path = "source\WorldServer\WorldDatabase.cpp"; Pattern = "RemoveSpawnPlacement.*spawn_location_placement" },
    @{ Name = "delete removes orphan spawn definition"; Path = "source\WorldServer\WorldDatabase.cpp"; Pattern = "remainingEntriesForSpawn == 0" },
    @{ Name = "groundspawn metadata persistence"; Path = "source\WorldServer\WorldDatabase.cpp"; Pattern = "spawn_ground.*groundspawn_id.*collection_skill" },
    @{ Name = "remote MariaDB non-SSL default"; Path = "source\common\dbcore.cpp"; Pattern = "MARIADB_TLS_DISABLE_PEER_VERIFICATION" },
    @{ Name = "zone removal clears preview pointer"; Path = "source\WorldServer\zoneserver.cpp"; Pattern = "GetDevPlacementPreviewSpawn\(\) == spawn" },
    @{ Name = "reload defers global spawn prototype deletion"; Path = "source\WorldServer\zoneserver.cpp"; Pattern = "if \(reloading\) \{\s+DeleteGlobalSpawns\(\);" },
    @{ Name = "reload suppresses respawn timer"; Path = "source\WorldServer\zoneserver.cpp"; Pattern = "!reloading && !LoadingData && respawn_timer\.Check" },
    @{ Name = "architecture doc"; Path = "docs\live_content_placement_architecture.md"; Pattern = "Existing Spawn Architecture" },
    @{ Name = "command doc"; Path = "docs\live_content_placement_commands.md"; Pattern = "/spawnpreview" },
    @{ Name = "harvestable workflow doc"; Path = "docs\live_content_placement_commands.md"; Pattern = "harvestable" },
    @{ Name = "spawn group workflow doc"; Path = "docs\live_content_placement_commands.md"; Pattern = "spawn group create" },
    @{ Name = "patrol marker workflow doc"; Path = "docs\live_content_placement_commands.md"; Pattern = "/location create" },
    @{ Name = "developer workflow doc"; Path = "docs\live_content_placement_developer_workflow.md"; Pattern = "Runtime validation" },
    @{ Name = "validation matrix doc"; Path = "docs\live_content_placement_validation_matrix.md"; Pattern = "Not Proven Autonomously" },
    @{ Name = "database connection helper"; Path = "scripts\live-placement-db-common.ps1"; Pattern = "Get-LivePlacementDbConnection" },
    @{ Name = "launch check script"; Path = "scripts\live-placement-launch-check.ps1"; Pattern = "eq2world stayed running" },
    @{ Name = "full stack launch check script"; Path = "scripts\live-placement-full-stack-check.ps1"; Pattern = "login stayed running" },
    @{ Name = "database smoke script"; Path = "scripts\live-placement-db-smoke.ps1"; Pattern = "Live placement database smoke validation passed" }
)

$results = foreach ($check in $checks) {
    Test-FileContains -RelativePath $check.Path -Pattern $check.Pattern -Name $check.Name
}

$failed = $results | Where-Object { -not $_.Passed }

foreach ($result in $results) {
    $status = if ($result.Passed) { "PASS" } else { "FAIL" }
    Write-Host ("[{0}] {1} ({2})" -f $status, $result.Name, $result.Detail)
}

if ($failed.Count -gt 0) {
    Write-Error ("Live placement validation failed: {0} check(s) failed." -f $failed.Count)
}

Write-Host "Live placement static validation passed."
