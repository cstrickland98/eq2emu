param(
    [string]$MysqlBinDir = "D:\Software\MySQL Server 8.4\bin",
    [string]$ConnectionFile = (Join-Path $PSScriptRoot "..\database-connection.txt"),
    [string]$HostName = "",
    [int]$Port = 3306,
    [string]$DatabaseUser = "",
    [string]$DatabasePassword = "",
    [string]$WorldDatabase = "eq2emu",
    [string]$LoginDatabase = "eq2ls"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "live-placement-db-common.ps1")

$Mysql = Join-Path $MysqlBinDir "mysql.exe"
if (!(Test-Path -LiteralPath $Mysql)) {
    throw "Required path not found: $Mysql"
}

$Connection = Get-LivePlacementDbConnection `
    -ConnectionFile $ConnectionFile `
    -HostName $HostName `
    -Port $Port `
    -DatabaseUser $DatabaseUser `
    -DatabasePassword $DatabasePassword `
    -WorldDatabase $WorldDatabase `
    -LoginDatabase $LoginDatabase

$Marker = "ralph_smoke_{0}_{1}" -f $PID, (Get-Date -Format "yyyyMMddHHmmss")

function Escape-Sql {
    param([string]$Value)

    return $Value.Replace("\", "\\").Replace("'", "''")
}

function Format-SqlFloat {
    param([double]$Value)

    return $Value.ToString([System.Globalization.CultureInfo]::InvariantCulture)
}

function Invoke-Sql {
    param(
        [string]$Sql,
        [switch]$AllowFailure,
        [string]$Database = $Connection.WorldDatabase
    )

    return Invoke-LivePlacementSql -Mysql $Mysql -Connection $Connection -Database $Database -Sql $Sql -AllowFailure:$AllowFailure
}

function Invoke-Scalar {
    param([string]$Sql)

    $Result = Invoke-Sql -Sql $Sql
    $Line = @($Result.Output | Where-Object { $null -ne $_ -and "$_".Trim().Length -gt 0 }) | Select-Object -Last 1
    if ($null -eq $Line) {
        throw "SQL returned no scalar value: $Sql"
    }

    return "$Line".Trim()
}

function Assert-Equal {
    param(
        [string]$Name,
        [string]$Actual,
        [string]$Expected
    )

    if ($Actual -ne $Expected) {
        throw "$Name failed. Expected '$Expected', got '$Actual'."
    }

    Write-Host "[PASS] $Name"
}

function Assert-True {
    param(
        [string]$Name,
        [bool]$Condition,
        [string]$Detail = ""
    )

    if (!$Condition) {
        throw "$Name failed. $Detail"
    }

    if ($Detail.Length -gt 0) {
        Write-Host "[PASS] $Name ($Detail)"
    }
    else {
        Write-Host "[PASS] $Name"
    }
}

function Assert-SqlFailure {
    param(
        [string]$Name,
        [string]$Sql
    )

    $Result = Invoke-Sql -Sql $Sql -AllowFailure
    if ($Result.ExitCode -eq 0) {
        throw "$Name failed. SQL unexpectedly succeeded: $Sql"
    }

    Write-Host "[PASS] $Name"
}

function New-Placement {
    param(
        [int]$SpawnId,
        [string]$Name,
        [int]$ZoneId,
        [double]$X,
        [double]$Y,
        [double]$Z,
        [double]$Heading
    )

    $SafeName = Escape-Sql $Name
    $LocationId = [int](Invoke-Scalar "INSERT INTO spawn_location_name (name) VALUES ('$SafeName'); SELECT LAST_INSERT_ID();")
    $EntryId = [int](Invoke-Scalar "INSERT INTO spawn_location_entry (spawn_id, spawn_location_id, spawnpercentage) VALUES ($SpawnId, $LocationId, 100); SELECT LAST_INSERT_ID();")
    $PlacementId = [int](Invoke-Scalar "INSERT INTO spawn_location_placement (zone_id, spawn_location_id, x, y, z, heading, respawn, duplicated_spawn) VALUES ($ZoneId, $LocationId, $(Format-SqlFloat $X), $(Format-SqlFloat $Y), $(Format-SqlFloat $Z), $(Format-SqlFloat $Heading), 300, 1); SELECT LAST_INSERT_ID();")

    return [pscustomobject]@{
        LocationId = $LocationId
        EntryId = $EntryId
        PlacementId = $PlacementId
    }
}

function Remove-SmokeRows {
    $SafeMarker = Escape-Sql $Marker
    $CleanupSql = @"
DELETE gc FROM spawn_location_group_chances gc JOIN spawn_location_group g ON gc.group_id = g.group_id WHERE g.name LIKE '$SafeMarker%';
DELETE FROM spawn_location_group WHERE name LIKE '$SafeMarker%';
DELETE ld FROM location_details ld JOIN locations l ON ld.location_id = l.id WHERE l.name LIKE '$SafeMarker%';
DELETE FROM locations WHERE name LIKE '$SafeMarker%';
DELETE p FROM spawn_location_placement p JOIN spawn_location_name n ON p.spawn_location_id = n.id WHERE n.name LIKE '$SafeMarker%';
DELETE e FROM spawn_location_entry e JOIN spawn_location_name n ON e.spawn_location_id = n.id WHERE n.name LIKE '$SafeMarker%';
DELETE FROM spawn_location_name WHERE name LIKE '$SafeMarker%';
DELETE sg FROM spawn_ground sg JOIN spawn s ON sg.spawn_id = s.id WHERE s.name LIKE '$SafeMarker%';
DELETE so FROM spawn_objects so JOIN spawn s ON so.spawn_id = s.id WHERE s.name LIKE '$SafeMarker%';
DELETE sn FROM spawn_npcs sn JOIN spawn s ON sn.spawn_id = s.id WHERE s.name LIKE '$SafeMarker%';
DELETE FROM spawn WHERE name LIKE '$SafeMarker%';
"@

    Invoke-Sql -Sql $CleanupSql | Out-Null
}

function New-SmokeGroupId {
    for ($Attempt = 0; $Attempt -lt 20; $Attempt++) {
        $Candidate = Get-Random -Minimum 1000000000 -Maximum 2000000000
        $Exists = Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_group WHERE group_id = $Candidate;"
        if ($Exists -eq "0") {
            return $Candidate
        }
    }

    throw "Could not find an unused smoke group id."
}

try {
    if (!(Test-LivePlacementDbReachable -Mysql $Mysql -Connection $Connection -Database $Connection.WorldDatabase)) {
        throw "Could not connect to $($Connection.WorldDatabase) at $($Connection.HostName):$($Connection.Port) as $($Connection.User)."
    }
    Write-Host "Connected to $($Connection.WorldDatabase) at $($Connection.HostName):$($Connection.Port)"

    foreach ($Table in @(
        "zones",
        "spawn",
        "spawn_objects",
        "spawn_npcs",
        "spawn_ground",
        "spawn_location_name",
        "spawn_location_entry",
        "spawn_location_placement",
        "spawn_location_group",
        "spawn_location_group_chances",
        "locations",
        "location_details"
    )) {
        Assert-Equal "table exists: $Table" (Invoke-Scalar "SELECT COUNT(*) FROM information_schema.TABLES WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '$Table';") "1"
    }

    Remove-SmokeRows
    $SafeMarker = Escape-Sql $Marker
    Assert-Equal "marker starts clean" (Invoke-Scalar "SELECT COUNT(*) FROM spawn WHERE name LIKE '$SafeMarker%';") "0"

    $ZoneId = [int](Invoke-Scalar "SELECT id FROM zones ORDER BY id LIMIT 1;")
    Assert-True "valid zone available" ($ZoneId -gt 0) "zone_id=$ZoneId"

    $ObjectName = Escape-Sql "${Marker}_object"
    $ObjectSpawnId = [int](Invoke-Scalar "INSERT INTO spawn (name, model_type, targetable, show_name) VALUES ('$ObjectName', 4084, 1, 1); SELECT LAST_INSERT_ID();")
    Invoke-Sql -Sql "INSERT INTO spawn_objects (spawn_id, device_id) VALUES ($ObjectSpawnId, 0);" | Out-Null
    $ObjectPlacement = New-Placement -SpawnId $ObjectSpawnId -Name "${Marker}_object_location" -ZoneId $ZoneId -X 1.0 -Y 2.0 -Z 3.0 -Heading 90.0
    Assert-Equal "object placement persisted" (Invoke-Scalar "SELECT COUNT(*) FROM spawn s JOIN spawn_objects so ON so.spawn_id = s.id JOIN spawn_location_entry e ON e.spawn_id = s.id JOIN spawn_location_placement p ON p.spawn_location_id = e.spawn_location_id WHERE s.id = $ObjectSpawnId AND p.id = $($ObjectPlacement.PlacementId) AND p.zone_id = $ZoneId;") "1"

    Assert-SqlFailure "duplicate object spawn metadata rejected" "INSERT INTO spawn_objects (spawn_id, device_id) VALUES ($ObjectSpawnId, 1);"
    Assert-SqlFailure "invalid zone placement rejected" "INSERT INTO spawn_location_placement (zone_id, spawn_location_id, x, y, z) VALUES (2147483647, $($ObjectPlacement.LocationId), 0, 0, 0);"

    Invoke-Sql -Sql "UPDATE spawn_location_placement SET x = 10.5, y = 20.25, z = 30.75, heading = 180.5, pitch = 5, roll = 1 WHERE id = $($ObjectPlacement.PlacementId);" | Out-Null
    Assert-Equal "position and heading update persisted" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_placement WHERE id = $($ObjectPlacement.PlacementId) AND ABS(x - 10.5) < 0.001 AND ABS(y - 20.25) < 0.001 AND ABS(z - 30.75) < 0.001 AND ABS(heading - 180.5) < 0.001 AND ABS(pitch - 5) < 0.001 AND ABS(roll - 1) < 0.001;") "1"

    $NpcName = Escape-Sql "${Marker}_npc"
    $NpcSpawnId = [int](Invoke-Scalar "INSERT INTO spawn (name, model_type, targetable, show_name, show_level, attackable, hp, power) VALUES ('$NpcName', 134, 1, 1, 1, 1, 100, 100); SELECT LAST_INSERT_ID();")
    Invoke-Sql -Sql "INSERT INTO spawn_npcs (spawn_id, min_level, max_level, enc_level, aggro_radius, ai_strategy) VALUES ($NpcSpawnId, 1, 1, 1, 5, 'BALANCED');" | Out-Null
    $NpcPlacement = New-Placement -SpawnId $NpcSpawnId -Name "${Marker}_npc_location" -ZoneId $ZoneId -X 4.0 -Y 5.0 -Z 6.0 -Heading 45.0
    Assert-Equal "npc placement persisted" (Invoke-Scalar "SELECT COUNT(*) FROM spawn s JOIN spawn_npcs sn ON sn.spawn_id = s.id JOIN spawn_location_entry e ON e.spawn_id = s.id JOIN spawn_location_placement p ON p.spawn_location_id = e.spawn_location_id WHERE s.id = $NpcSpawnId AND p.id = $($NpcPlacement.PlacementId);") "1"

    $HarvestName = Escape-Sql "${Marker}_harvestable"
    $HarvestSpawnId = [int](Invoke-Scalar "INSERT INTO spawn (name, model_type, targetable, show_name) VALUES ('$HarvestName', 7251, 1, 1); SELECT LAST_INSERT_ID();")
    Invoke-Sql -Sql "INSERT INTO spawn_ground (spawn_id, number_harvests, num_attempts_per_harvest, groundspawn_id, collection_skill, randomize_heading) VALUES ($HarvestSpawnId, 3, 1, 900001, 'Gathering', 1);" | Out-Null
    $HarvestPlacement = New-Placement -SpawnId $HarvestSpawnId -Name "${Marker}_harvestable_location" -ZoneId $ZoneId -X 7.0 -Y 8.0 -Z 9.0 -Heading 10.0
    Assert-Equal "harvestable metadata persisted" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_ground sg JOIN spawn_location_entry e ON e.spawn_id = sg.spawn_id JOIN spawn_location_placement p ON p.spawn_location_id = e.spawn_location_id WHERE sg.spawn_id = $HarvestSpawnId AND sg.groundspawn_id = 900001 AND sg.collection_skill = 'Gathering' AND p.id = $($HarvestPlacement.PlacementId);") "1"

    $CollectionName = Escape-Sql "${Marker}_collection"
    $CollectionSpawnId = [int](Invoke-Scalar "INSERT INTO spawn (name, model_type, targetable, show_name) VALUES ('$CollectionName', 7252, 1, 1); SELECT LAST_INSERT_ID();")
    Invoke-Sql -Sql "INSERT INTO spawn_ground (spawn_id, number_harvests, num_attempts_per_harvest, groundspawn_id, collection_skill, randomize_heading) VALUES ($CollectionSpawnId, 1, 1, 900002, 'Collecting', 1);" | Out-Null
    $CollectionPlacement = New-Placement -SpawnId $CollectionSpawnId -Name "${Marker}_collection_location" -ZoneId $ZoneId -X 11.0 -Y 12.0 -Z 13.0 -Heading 15.0
    Assert-Equal "collection metadata persisted" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_ground sg JOIN spawn_location_entry e ON e.spawn_id = sg.spawn_id JOIN spawn_location_placement p ON p.spawn_location_id = e.spawn_location_id WHERE sg.spawn_id = $CollectionSpawnId AND sg.groundspawn_id = 900002 AND sg.collection_skill = 'Collecting' AND p.id = $($CollectionPlacement.PlacementId);") "1"

    $GroupId = New-SmokeGroupId
    $GroupName = Escape-Sql "${Marker}_camp_group"
    Invoke-Sql -Sql "INSERT INTO spawn_location_group (group_id, placement_id, name) VALUES ($GroupId, $($ObjectPlacement.PlacementId), '$GroupName'), ($GroupId, $($NpcPlacement.PlacementId), '$GroupName');" | Out-Null
    Invoke-Sql -Sql "INSERT INTO spawn_location_group_chances (group_id, percentage) VALUES ($GroupId, 100);" | Out-Null
    Assert-Equal "spawn group persisted" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_group g JOIN spawn_location_group_chances gc ON gc.group_id = g.group_id WHERE g.group_id = $GroupId AND g.name LIKE '$SafeMarker%';") "2"

    $PatrolName = Escape-Sql "${Marker}_patrol"
    $PatrolId = [int](Invoke-Scalar "INSERT INTO locations (zone_id, name, include_y, discovery) VALUES ($ZoneId, '$PatrolName', 1, 0); SELECT LAST_INSERT_ID();")
    Invoke-Sql -Sql "INSERT INTO location_details (location_id, x, y, z) VALUES ($PatrolId, 1, 2, 3), ($PatrolId, 4, 5, 6), ($PatrolId, 7, 8, 9);" | Out-Null
    Assert-Equal "patrol markers persisted" (Invoke-Scalar "SELECT COUNT(*) FROM locations l JOIN location_details ld ON ld.location_id = l.id WHERE l.id = $PatrolId AND l.name LIKE '$SafeMarker%';") "3"

    $SpawnCountAfterReconnect = Invoke-Scalar "SELECT COUNT(*) FROM spawn WHERE name LIKE '$SafeMarker%';"
    $PlacementCountAfterReconnect = Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_placement p JOIN spawn_location_name n ON n.id = p.spawn_location_id WHERE n.name LIKE '$SafeMarker%';"
    Assert-Equal "spawns visible on a new DB connection" $SpawnCountAfterReconnect "4"
    Assert-Equal "placements visible on a new DB connection" $PlacementCountAfterReconnect "4"
    Assert-Equal "updated placement visible on a new DB connection" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_placement WHERE id = $($ObjectPlacement.PlacementId) AND ABS(heading - 180.5) < 0.001;") "1"

    Invoke-Sql -Sql "DELETE FROM spawn_location_placement WHERE id = $($ObjectPlacement.PlacementId);" | Out-Null
    Assert-Equal "placement delete removed row" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_placement WHERE id = $($ObjectPlacement.PlacementId);") "0"
    Assert-Equal "placement delete cascaded group row" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_group WHERE placement_id = $($ObjectPlacement.PlacementId);") "0"

    Remove-SmokeRows
    Assert-Equal "smoke spawn rows cleaned" (Invoke-Scalar "SELECT COUNT(*) FROM spawn WHERE name LIKE '$SafeMarker%';") "0"
    Assert-Equal "smoke location rows cleaned" (Invoke-Scalar "SELECT COUNT(*) FROM locations WHERE name LIKE '$SafeMarker%';") "0"
    Assert-Equal "smoke spawn location rows cleaned" (Invoke-Scalar "SELECT COUNT(*) FROM spawn_location_name WHERE name LIKE '$SafeMarker%';") "0"

    Write-Host "Live placement database smoke validation passed."
}
finally {
    try {
        if (Test-LivePlacementDbReachable -Mysql $Mysql -Connection $Connection -Database $Connection.WorldDatabase) {
            Remove-SmokeRows
        }
    }
    catch {
        Write-Warning "Smoke cleanup failed: $_"
    }
}
