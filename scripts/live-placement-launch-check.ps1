param(
    [string]$MysqlBinDir = "D:\Software\MySQL Server 8.4\bin",
    [string]$ConnectionFile = (Join-Path $PSScriptRoot "..\database-connection.txt"),
    [string]$HostName = "",
    [int]$Port = 3306,
    [string]$DatabaseUser = "",
    [string]$DatabasePassword = "",
    [string]$WorldDatabase = "eq2emu",
    [string]$LoginDatabase = "eq2ls",
    [string]$WorldExe = (Join-Path $PSScriptRoot "..\build\windows-msvc\bin\eq2world.exe"),
    [string]$ServerDir = (Join-Path $PSScriptRoot "..\server"),
    [int]$ObserveSeconds = 20
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "live-placement-db-common.ps1")

$Mysql = Join-Path $MysqlBinDir "mysql.exe"
foreach ($Path in @($Mysql, $WorldExe, $ServerDir)) {
    if (!(Test-Path -LiteralPath $Path)) {
        throw "Required path not found: $Path"
    }
}

$Connection = Get-LivePlacementDbConnection `
    -ConnectionFile $ConnectionFile `
    -HostName $HostName `
    -Port $Port `
    -DatabaseUser $DatabaseUser `
    -DatabasePassword $DatabasePassword `
    -WorldDatabase $WorldDatabase `
    -LoginDatabase $LoginDatabase

$ResolvedServerDir = (Resolve-Path $ServerDir).Path
$ResolvedWorldExe = (Resolve-Path $WorldExe).Path
$WorldIni = Join-Path $ResolvedServerDir "world_db.ini"
$WorldIniBackup = if (Test-Path -LiteralPath $WorldIni) { Get-Content -Raw -LiteralPath $WorldIni } else { $null }
$WorldProcess = $null

try {
    if (!(Test-LivePlacementDbReachable -Mysql $Mysql -Connection $Connection -Database $Connection.WorldDatabase)) {
        throw "Could not connect to $($Connection.WorldDatabase) at $($Connection.HostName):$($Connection.Port) as $($Connection.User)."
    }
    Write-Host "world database reachable at $($Connection.HostName):$($Connection.Port)"

    Write-LivePlacementDbIni -Path $WorldIni -Database $Connection.WorldDatabase -Connection $Connection
    Write-Host "temporary world_db.ini written for launch check"

    $WorldProcess = Start-Process -FilePath $ResolvedWorldExe -WorkingDirectory $ResolvedServerDir -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds $ObserveSeconds
    if ($WorldProcess.HasExited) {
        throw "eq2world exited early with code $($WorldProcess.ExitCode)"
    }

    Write-Host "eq2world stayed running for launch check with pid $($WorldProcess.Id)"
}
finally {
    if ($WorldProcess -and !$WorldProcess.HasExited) {
        Stop-Process -Id $WorldProcess.Id -Force
        Write-Host "eq2world stopped after launch check"
    }

    if ($null -ne $WorldIniBackup) {
        Set-Content -LiteralPath $WorldIni -Value $WorldIniBackup -Encoding ASCII -NoNewline
    }
}
