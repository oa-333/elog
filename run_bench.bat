@echo off
setlocal enabledelayedexpansion enableextensions

REM change this to actual destination
set INSTALL_DIR=C:\install
set DEV_DIR=%~dp0

REM build elog
call .\build.bat --rel-with-debug-info --verbose
IF errorlevel 1 (
    echo ERROR: fBuild failed
    exit /b 1
)
echo INFO: Build success

REM exec benchmark
pushd %INSTALL_DIR%\bin\Windows-RelWithDebInfo

echo INFO: Creating bench_data directory
IF NOT EXIST bench_data (
    mkdir bench_data
)
echo INFO: bench_data directory created

echo INFO: Purging bench_data directory from previous run results
del /Q bench_data\*

echo INFO: Executing benchmark
REM xcopy /Y %INSTALL_DIR%\elog\bin\* .
.\elog_bench.exe

REM TODO: plot this with mingw gnuplot
REM plot
xcopy /Y %DEV_DIR%\src\elog_bench\gnuplot\*.gp .
for %%x in (*.gp) DO (
    C:\msys64\ucrt64\bin\gnuplot.exe %%x
)

REM copy results back
xcopy /Y .\*.png %DEV_DIR%\src\elog_bench\png\

popd