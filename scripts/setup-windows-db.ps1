param(
    [string]$DatabaseDir = (Join-Path $PSScriptRoot "..\..\eq2emu-database"),
    [string]$MysqlBinDir = "",
    [string]$HostName = "127.0.0.1",
    [int]$Port = 3306,
    [string]$RootUser = "root",
    [string]$RootPassword = "pass",
    [string]$LoginDatabase = "eq2ls",
    [string]$WorldDatabase = "eq2emu",
    [string]$DataDir = (Join-Path $PSScriptRoot "..\build\mysql-data"),
    [switch]$StartLocalServer,
    [switch]$ForceImport,
    [switch]$SkipImport,
    [switch]$NoSeedWorldServer,
    [string]$WorldName = "TestLabs",
    [string]$WorldAccount = "testlabs",
    [string]$WorldPassword = "testpass",
    [string[]]$ServerExtraArgs = @()
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

function Resolve-Tool {
    param([string]$Name)

    $Candidates = @()
    if ($MysqlBinDir) {
        $Candidates += (Join-Path $MysqlBinDir $Name)
    }

    $Command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($Command) {
        $Candidates += $Command.Source
    }

    $Candidates += @(
        (Join-Path $env:ProgramFiles "MySQL\MySQL Server 8.4\bin\$Name"),
        (Join-Path $env:ProgramFiles "MySQL\MySQL Server 8.0\bin\$Name"),
        (Join-Path $env:ProgramFiles "MariaDB 11.4\bin\$Name"),
        (Join-Path $env:ProgramFiles "MariaDB 11.3\bin\$Name"),
        (Join-Path $env:ProgramFiles "MariaDB 10.11\bin\$Name"),
        "D:\Software\MySQL Server 8.4\bin\$Name",
        "D:\Software\MariaDB 11.4\bin\$Name",
        "D:\Software\MariaDB 10.11\bin\$Name"
    )

    foreach ($Candidate in $Candidates) {
        if ($Candidate -and (Test-Path $Candidate)) {
            return (Resolve-Path $Candidate).Path
        }
    }

    throw "$Name was not found. Install MariaDB/MySQL client tools or pass -MysqlBinDir."
}

function Get-MysqlArgs {
    param(
        [string]$Database = ""
    )

    $Args = @(
        "--protocol=TCP",
        "--host=$HostName",
        "--port=$Port",
        "--user=$RootUser"
    )

    if ($Database) {
        $Args += "--database=$Database"
    }

    return $Args
}

function Invoke-MysqlClient {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [switch]$NoPassword,
        [switch]$Quiet,
        [switch]$CaptureOutput
    )

    $OldMysqlPwd = $env:MYSQL_PWD
    $HadMysqlPwd = Test-Path Env:MYSQL_PWD
    $OldErrorActionPreference = $ErrorActionPreference

    if (!$NoPassword -and !$script:MysqlNoPassword -and $RootPassword.Length -gt 0) {
        $env:MYSQL_PWD = $RootPassword
    }
    else {
        Remove-Item Env:MYSQL_PWD -ErrorAction SilentlyContinue
    }

    $ErrorActionPreference = "Continue"
    try {
        if ($CaptureOutput) {
            $Output = & $FilePath @Arguments 2>&1
        }
        elseif ($Quiet) {
            & $FilePath @Arguments *> $null
            $Output = $null
        }
        else {
            $Output = & $FilePath @Arguments
        }
        $script:LastNativeExitCode = $LASTEXITCODE
        return $Output
    }
    finally {
        $ErrorActionPreference = $OldErrorActionPreference
        if ($HadMysqlPwd) {
            $env:MYSQL_PWD = $OldMysqlPwd
        }
        else {
            Remove-Item Env:MYSQL_PWD -ErrorAction SilentlyContinue
        }
    }
}

function Test-MysqlPing {
    param([switch]$NoPassword)

    $Args = Get-MysqlArgs
    $Args += "ping"
    $null = Invoke-MysqlClient -FilePath $script:MysqlAdminExe -Arguments $Args -NoPassword:$NoPassword -Quiet
    return $script:LastNativeExitCode -eq 0
}

function Test-MysqlLogin {
    param([switch]$NoPassword)

    $Args = Get-MysqlArgs
    $Args += @("--batch", "--skip-column-names", "--execute=SELECT 1;")
    $null = Invoke-MysqlClient -FilePath $script:MysqlExe -Arguments $Args -NoPassword:$NoPassword -Quiet
    return $script:LastNativeExitCode -eq 0
}

function Wait-ForMysql {
    for ($Attempt = 0; $Attempt -lt 60; $Attempt++) {
        if (Test-MysqlLogin) {
            $script:MysqlNoPassword = $false
            return
        }
        if (Test-MysqlLogin -NoPassword) {
            $script:MysqlNoPassword = $true
            return
        }
        Start-Sleep -Seconds 1
    }

    throw "Timed out waiting for MariaDB/MySQL on ${HostName}:$Port."
}

function Invoke-Mysql {
    param(
        [string]$Sql,
        [string]$Database = "",
        [switch]$NoPassword
    )

    $Args = Get-MysqlArgs -Database $Database
    $Args += "--execute=$Sql"
    $null = Invoke-MysqlClient -FilePath $script:MysqlExe -Arguments $Args -NoPassword:$NoPassword
    if ($script:LastNativeExitCode -ne 0) {
        throw "mysql.exe failed with exit code $script:LastNativeExitCode."
    }
}

function Get-MysqlScalar {
    param(
        [string]$Sql,
        [string]$Database = ""
    )

    $Args = Get-MysqlArgs -Database $Database
    $Args += @("--batch", "--skip-column-names", "--execute=$Sql")
    $Output = Invoke-MysqlClient -FilePath $script:MysqlExe -Arguments $Args -CaptureOutput
    if ($script:LastNativeExitCode -ne 0) {
        throw "mysql.exe failed with exit code $script:LastNativeExitCode. $Output"
    }

    return ($Output | Select-Object -First 1)
}

function Escape-SqlLiteral {
    param([string]$Value)
    return $Value.Replace("\", "\\").Replace("'", "''")
}

function ConvertTo-Sha512Hex {
    param([string]$Value)

    $Sha = [System.Security.Cryptography.SHA512]::Create()
    try {
        $Bytes = [System.Text.Encoding]::UTF8.GetBytes($Value)
        return ([BitConverter]::ToString($Sha.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $Sha.Dispose()
    }
}

function Test-DatabaseExists {
    param([string]$Name)

    $EscapedName = Escape-SqlLiteral $Name
    $Result = Get-MysqlScalar "SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME='$EscapedName';"
    return $Result -eq $Name
}

function Ensure-Database {
    param([string]$Name)

    $EscapedName = $Name.Replace("``", "````")
    if ($ForceImport) {
        Invoke-Mysql "DROP DATABASE IF EXISTS ``$EscapedName``;"
    }
    Invoke-Mysql "CREATE DATABASE IF NOT EXISTS ``$EscapedName``;"
}

function Import-Archive {
    param(
        [string]$Database,
        [string]$ArchiveName,
        [switch]$AllowForce
    )

    $ArchivePath = Join-Path $DatabaseDir $ArchiveName
    if (!(Test-Path $ArchivePath)) {
        throw "$ArchivePath was not found. Clone or place the EQ2Emu database dumps next to this repository, or pass -DatabaseDir."
    }

    $MysqlArgs = Get-MysqlArgs -Database $Database
    if ($AllowForce) {
        $MysqlArgs += "--force"
    }

    Write-Host "Importing $ArchiveName into $Database..."
    $OldMysqlPwd = $env:MYSQL_PWD
    $HadMysqlPwd = Test-Path Env:MYSQL_PWD
    $OldErrorActionPreference = $ErrorActionPreference
    if (!$script:MysqlNoPassword -and $RootPassword.Length -gt 0) {
        $env:MYSQL_PWD = $RootPassword
    }
    else {
        Remove-Item Env:MYSQL_PWD -ErrorAction SilentlyContinue
    }

    $ErrorActionPreference = "Continue"
    try {
        & tar -xOzf $ArchivePath | & $script:MysqlExe @MysqlArgs
        $ExitCode = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $OldErrorActionPreference
        if ($HadMysqlPwd) {
            $env:MYSQL_PWD = $OldMysqlPwd
        }
        else {
            Remove-Item Env:MYSQL_PWD -ErrorAction SilentlyContinue
        }
    }

    if ($ExitCode -ne 0 -and !$AllowForce) {
        throw "Import failed with exit code $ExitCode."
    }
    if ($ExitCode -ne 0 -and $AllowForce) {
        Write-Warning "Import completed with mysql --force and exit code $ExitCode. Review the dump output above."
    }
}

function Start-LocalDatabase {
    $script:MysqldExe = Resolve-Tool "mysqld.exe"

    if ((Test-MysqlPing) -or (Test-MysqlPing -NoPassword)) {
        Write-Host "MariaDB/MySQL is already reachable on ${HostName}:$Port."
        return
    }

    $ResolvedDataDir = $DataDir
    if (!(Test-Path $ResolvedDataDir)) {
        New-Item -ItemType Directory -Path $ResolvedDataDir -Force | Out-Null
    }
    $ResolvedDataDir = (Resolve-Path $ResolvedDataDir).Path
    $BaseDir = Split-Path (Split-Path $script:MysqldExe -Parent) -Parent

    if (!(Test-Path (Join-Path $ResolvedDataDir "mysql"))) {
        Write-Host "Initializing local database directory at $ResolvedDataDir..."
        & $script:MysqldExe "--initialize-insecure" "--basedir=$BaseDir" "--datadir=$ResolvedDataDir" "--console"
        if ($LASTEXITCODE -ne 0) {
            throw "mysqld --initialize-insecure failed with exit code $LASTEXITCODE."
        }
        $script:InitializedLocalDataDir = $true
    }

    $ServerArgs = @(
        "--basedir=`"$BaseDir`"",
        "--datadir=`"$ResolvedDataDir`"",
        "--port=$Port",
        "--bind-address=$HostName",
        "--mysqlx=0",
        "--log-error=`"$(Join-Path $ResolvedDataDir 'mysql.err')`""
    )

    $MysqldVersion = & $script:MysqldExe --version
    if ($MysqldVersion -match "MySQL") {
        $ServerArgs += "--sql-mode=NO_ENGINE_SUBSTITUTION"
    }
    if ($MysqldVersion -match "MySQL" -and $MysqldVersion -match "Ver 8\.4") {
        $ServerArgs += "--skip-restrict-fk-on-non-standard-key"
    }
    $ServerArgs += $ServerExtraArgs

    Write-Host "Starting local database server on ${HostName}:$Port..."
    $script:DatabaseProcess = Start-Process -FilePath $script:MysqldExe -ArgumentList ($ServerArgs -join " ") -WorkingDirectory $RepoRoot -WindowStyle Hidden -PassThru
}

function Seed-WorldServerAccount {
    $Hash = ConvertTo-Sha512Hex $WorldPassword
    $Name = Escape-SqlLiteral $WorldName
    $Account = Escape-SqlLiteral $WorldAccount
    $PasswordHash = Escape-SqlLiteral $Hash
    $Address = Escape-SqlLiteral $HostName

    $Sql = @"
INSERT INTO login_worldservers
    (name, disabled, account, description, server_type, password, note, ip_address, created_date, server_category)
VALUES
    ('$Name', 0, '$Account', 'Local Windows test server', 'Development', '$PasswordHash', '', '$Address', UNIX_TIMESTAMP(), 'Development')
ON DUPLICATE KEY UPDATE
    disabled = 0,
    password = VALUES(password),
    ip_address = VALUES(ip_address),
    server_category = VALUES(server_category);
"@

    Invoke-Mysql -Database $LoginDatabase -Sql $Sql
    Write-Host "Seeded login_worldservers account '$WorldAccount' for world '$WorldName'."
}

$script:MysqlExe = Resolve-Tool "mysql.exe"
$script:MysqlAdminExe = Resolve-Tool "mysqladmin.exe"
$script:MysqlNoPassword = $false
$script:InitializedLocalDataDir = $false

if ($StartLocalServer) {
    Start-LocalDatabase
}

Wait-ForMysql

if ($script:MysqlNoPassword -and $script:InitializedLocalDataDir -and $RootPassword.Length -gt 0) {
    $EscapedPassword = Escape-SqlLiteral $RootPassword
    Invoke-Mysql -NoPassword -Sql "ALTER USER '$RootUser'@'localhost' IDENTIFIED BY '$EscapedPassword'; FLUSH PRIVILEGES;"
    $script:MysqlNoPassword = $false
}

$ServerVersion = Get-MysqlScalar "SELECT VERSION();"
$UsingOracleMysql = $ServerVersion -and ($ServerVersion -notmatch "MariaDB")
Write-Host "Using database server version: $ServerVersion"
if ($UsingOracleMysql) {
    Write-Warning "The EQ2Emu world dump is MariaDB-oriented. MySQL can launch the server, but MariaDB is recommended for full sequence compatibility."
}

if (!$SkipImport) {
    $LoginExists = Test-DatabaseExists $LoginDatabase
    $WorldExists = Test-DatabaseExists $WorldDatabase

    Ensure-Database $LoginDatabase
    Ensure-Database $WorldDatabase

    if ($ForceImport -or !$LoginExists) {
        Import-Archive -Database $LoginDatabase -ArchiveName "eq2emu_login_db.tar.gz"
    }
    else {
        Write-Host "$LoginDatabase already exists; skipping login import. Use -ForceImport to reload it."
    }

    if ($ForceImport -or !$WorldExists) {
        Import-Archive -Database $WorldDatabase -ArchiveName "eq2emu_world_db.tar.gz" -AllowForce:$UsingOracleMysql
    }
    else {
        Write-Host "$WorldDatabase already exists; skipping world import. Use -ForceImport to reload it."
    }
}

if (!$NoSeedWorldServer) {
    Seed-WorldServerAccount
}

Write-Host "Windows database setup complete."
