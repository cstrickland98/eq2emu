$ErrorActionPreference = "Stop"

function Get-LivePlacementDbConnection {
    param(
        [string]$ConnectionFile = (Join-Path $PSScriptRoot "..\database-connection.txt"),
        [string]$HostName = "",
        [int]$Port = 3306,
        [string]$DatabaseUser = "",
        [string]$DatabasePassword = "",
        [string]$WorldDatabase = "eq2emu",
        [string]$LoginDatabase = "eq2ls"
    )

    if (Test-Path -LiteralPath $ConnectionFile) {
        $Text = Get-Content -Raw -LiteralPath $ConnectionFile
        if ($Text -match "located\s+at\s+(?<host>\S+)\s+user=(?<user>\S+)\s+and\s+pass=(?<pass>\S+)") {
            if ($HostName.Length -eq 0) {
                $HostName = $Matches.host
            }
            if ($DatabaseUser.Length -eq 0) {
                $DatabaseUser = $Matches.user
            }
            if ($DatabasePassword.Length -eq 0) {
                $DatabasePassword = $Matches.pass
            }
        }
        else {
            foreach ($Line in ($Text -split "\r?\n")) {
                if ($Line -match "^\s*host\s*=\s*(?<value>\S+)") {
                    if ($HostName.Length -eq 0) {
                        $HostName = $Matches.value
                    }
                }
                elseif ($Line -match "^\s*user\s*=\s*(?<value>\S+)") {
                    if ($DatabaseUser.Length -eq 0) {
                        $DatabaseUser = $Matches.value
                    }
                }
                elseif ($Line -match "^\s*(password|pass)\s*=\s*(?<value>\S+)") {
                    if ($DatabasePassword.Length -eq 0) {
                        $DatabasePassword = $Matches.value
                    }
                }
            }
        }
    }

    if ($HostName.Length -eq 0 -or $DatabaseUser.Length -eq 0 -or $DatabasePassword.Length -eq 0) {
        throw "Database connection is incomplete. Provide database-connection.txt or pass -HostName, -DatabaseUser, and -DatabasePassword."
    }

    return [pscustomobject]@{
        HostName = $HostName
        Port = $Port
        User = $DatabaseUser
        Password = $DatabasePassword
        WorldDatabase = $WorldDatabase
        LoginDatabase = $LoginDatabase
    }
}

function Invoke-LivePlacementSql {
    param(
        [string]$Mysql,
        [pscustomobject]$Connection,
        [string]$Sql,
        [string]$Database = $Connection.WorldDatabase,
        [switch]$AllowFailure
    )

    $Args = @(
        "--protocol=TCP",
        "--host=$($Connection.HostName)",
        "--port=$($Connection.Port)",
        "--user=$($Connection.User)",
        "--batch",
        "--raw",
        "--skip-column-names"
    )

    if ($Database.Length -gt 0) {
        $Args += "--database=$Database"
    }

    $Args += "--execute=$Sql"

    $HadMysqlPwd = Test-Path Env:MYSQL_PWD
    $OldMysqlPwd = $env:MYSQL_PWD
    $OldErrorActionPreference = $ErrorActionPreference
    $NativePreference = Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue
    if ($NativePreference) {
        $OldNativePreference = $PSNativeCommandUseErrorActionPreference
        $PSNativeCommandUseErrorActionPreference = $false
    }

    try {
        $env:MYSQL_PWD = $Connection.Password
        $ErrorActionPreference = "Continue"
        $Output = & $Mysql @Args 2>&1
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

        if ($NativePreference) {
            $PSNativeCommandUseErrorActionPreference = $OldNativePreference
        }
    }

    if ($ExitCode -ne 0 -and !$AllowFailure) {
        throw "mysql failed with exit code $ExitCode`n$($Output -join [Environment]::NewLine)`nSQL: $Sql"
    }

    return [pscustomobject]@{
        ExitCode = $ExitCode
        Output = @($Output)
    }
}

function Test-LivePlacementDbReachable {
    param(
        [string]$Mysql,
        [pscustomobject]$Connection,
        [string]$Database = $Connection.WorldDatabase
    )

    $Result = Invoke-LivePlacementSql -Mysql $Mysql -Connection $Connection -Database $Database -Sql "SELECT 1;" -AllowFailure
    return $Result.ExitCode -eq 0
}

function Write-LivePlacementDbIni {
    param(
        [string]$Path,
        [string]$Database,
        [pscustomobject]$Connection
    )

    $Content = @(
        "# Temporary live placement validation database config.",
        "[Database]",
        "host=$($Connection.HostName)",
        "user=$($Connection.User)",
        "password=$($Connection.Password)",
        "database=$Database",
        ""
    )

    Set-Content -LiteralPath $Path -Value $Content -Encoding ASCII
}
