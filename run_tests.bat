@echo off
setlocal enabledelayedexpansion enableextensions

REM change this to actual destination
set INSTALL_DIR=C:\install
set DEV_DIR=%~dp0

REM build elog (enable all extensions and redis publishing)
call .\build.bat --full --config-publish redis
IF errorlevel 1 (
    echo ERROR: Build failed
    exit /b 1
)
echo INFO: Build success

REM exec tests
set ELOG_BUILD_PATH=%CD%\cmake_build\Windows-Debug
echo "ELOG_BUILD_PATH=%ELOG_BUILD_PATH%"
pushd %INSTALL_DIR%\bin\Windows-Debug
echo "Running tests at: %CD%"


REM prepare SQLite db file
del -Y test.db
sqlite3 test.db "create table log_records (rid int64, time varchar(64), level varchar(64), host varchar(64), user varchar(64), prog varchar(64), pid int64, tid int64, mod varchar(64), src varchar(64), msg varchar(1024));"

REM set env var used by test
set TEST_ENV_VAR=TEST_ENV_VALUE

echo INFO: Creating test_data directory
IF NOT EXIST test_data (
    mkdir test_data
)
echo INFO: test_data directory created

echo INFO: Purging test_data directory from previous test runs
del /Q test_data\*

echo Running elog_pm shared memory guardian
start elog_pm.exe --shm-guard

echo INFO: Running tests
echo DEBUG: Running command .\elog_test.exe %*
.\elog_test.exe %*

echo kill elog_pm
taskkill /im "elog_pm.exe" /f /t >nul 2>&1

popd