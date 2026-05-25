@echo off
setlocal

set "SERVER_DIR=%~dp0"
for %%I in ("%SERVER_DIR%..") do set "ROOT_DIR=%%~fI"

set "LOGIN_EXE=%ROOT_DIR%\build\windows-msvc\bin\login.exe"
set "WORLD_EXE=%ROOT_DIR%\build\windows-msvc\bin\eq2world.exe"

if not exist "%LOGIN_EXE%" (
    echo Missing rebuilt login executable:
    echo   %LOGIN_EXE%
    exit /b 1
)

if not exist "%WORLD_EXE%" (
    echo Missing rebuilt world executable:
    echo   %WORLD_EXE%
    exit /b 1
)

if not exist "%SERVER_DIR%server_config.json" (
    echo This script should be run from the eq2emu\server folder.
    echo Missing:
    echo   %SERVER_DIR%server_config.json
    exit /b 1
)

set "MARIADB_TLS_DISABLE_PEER_VERIFICATION=1"

echo Dev build executable stamps:
call :PrintBuildStamp "login.exe" "%LOGIN_EXE%"
call :PrintBuildStamp "eq2world.exe" "%WORLD_EXE%"
echo.

echo Starting rebuilt login server...
start "EQ2Emu Login" /D "%SERVER_DIR%" "%LOGIN_EXE%"

echo Waiting briefly before starting world server...
timeout /t 3 /nobreak >nul

echo Starting rebuilt world server...
start "EQ2Emu World" /D "%SERVER_DIR%" "%WORLD_EXE%"

echo.
echo Started rebuilt servers from:
echo   %ROOT_DIR%\build\windows-msvc\bin
echo Working directory:
echo   %SERVER_DIR%

endlocal
exit /b 0

:PrintBuildStamp
set "BUILD_LABEL=%~1"
set "BUILD_PATH=%~2"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p = $env:BUILD_PATH; $i = Get-Item -LiteralPath $p; $sha = [System.Security.Cryptography.SHA256]::Create(); $stream = [System.IO.File]::OpenRead($p); try { $h = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').Substring(0, 12) } finally { $stream.Dispose(); $sha.Dispose() }; Write-Output ('  {0,-12} built {1:yyyy-MM-dd HH:mm:ss zzz}  size {2:N0} bytes  sha256 {3}' -f $env:BUILD_LABEL, $i.LastWriteTime, $i.Length, $h)"
if errorlevel 1 (
    for %%I in ("%BUILD_PATH%") do echo   %BUILD_LABEL% built %%~tI  size %%~zI bytes
)
exit /b 0
