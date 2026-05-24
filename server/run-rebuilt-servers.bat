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
