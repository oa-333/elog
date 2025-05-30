@echo off
setlocal enabledelayedexpansion enableextensions

REM Possible command line options:
REM -v|--verbose
REM -d|--debug
REM -r|--release
REM -w|--rel-with-debug-info
REM -c|--conn sqlite|mysql|postgresql|kafka
REM -i|--install-dir <INSTALL_DIR>

REM set default values
SET PLATFORM=WINDOWS
SET BUILD_TYPE=Debug
SET INSTALL_DIR=C:\install\elog
SET STACK_TRACE=0

SET CONN_INDEX=0
SET CONNS[0]=tmp
echo [DEBUG] Parsing args
:GET_OPTS
echo DEBUG: processing option "%1" "%2"
IF /I "%1" == "-v" SET VERBOSE=1
IF /I "%1" == "--verbose" SET VERBOSE=1
IF /I "%1" == "-d" SET BUILD_TYPE=Debug
IF /I "%1" == "--debug" SET BUILD_TYPE=Debug
IF /I "%1" == "-r" SET BUILD_TYPE=Release
IF /I "%1" == "--release" SET BUILD_TYPE=Release
IF /I "%1" == "-w" SET BUILD_TYPE=RelWithDebInfo
IF /I "%1" == "--rel-with-debug-info" SET BUILD_TYPE=RelWithDebInfo
IF /I "%1" == "--stack-trace" SET STACK_TRACE=1
IF /I "%1" == "-s" SET STACK_TRACE=1
IF /I "%1" == "-c" SET CONNS[!CONN_INDEX!]=%2 & SET /A CONN_INDEX+=1 & shift
IF /I "%1" == "--conn" SET CONNS[!CONN_INDEX!]=%2 & SET /A CONN_INDEX+=1 & shift
IF /I "%1" == "-i" SET INSTALL_DIR=%2 & shift
IF /I "%1" == "--install-dir" SET INSTALL_DIR=%2 & shift
REM TODO: not checking for illegal parameters
shift
IF "%1" == "--" GOTO SET_OPTS
IF NOT "%1" == "" GOTO GET_OPTS

:SET_OPTS
echo [DEBUG] Args parsed, options left: %*

REM set normal options
echo [INFO] Build type: $BUILD_TYPE
echo [INFO] Install dir: $INSTALL_DIR
SET OPTS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%
IF "%VERBOSE%" == "1" SET OPTS=%OPTS% -DCMAKE_VERBOSE_MAKEFILE=ON
IF "%STACK_TRACE%" == "1" SET OPTS=%OPTS% -DELOG_ENABLE_STACK_TRACE=ON
echo [DEBUG] Current options: %OPTS%

REM add optional connectors
REM TODO: this is not working, for some reason the connector name has extra trailing whitespace
echo [DEBUG] Parsing %CONN_INDEX% connectors
set /A CONN_COUNT=%CONN_INDEX-1
for /l %%n in (0,1,%CONN_COUNT%) do (
    set conn=!CONNS[%%n]!
    echo [DEBUG] Adding connector --!conn!-- to OPTS %OPTS%
    IF "!conn!" == "sqlite" SET OPTS=!OPTS! -DELOG_ENABLE_SQLITE_DB_CONNECTOR=ON
    IF "!conn!" == "mysql" SET OPTS=%OPTS% -DELOG_ENABLE_MYSQL_DB_CONNECTOR=ON
    IF "!conn!" == "postgresql" SET OPTS=!OPTS! -DELOG_ENABLE_POSTGRESQL_DB_CONNECTOR=ON
    IF "!conn!" == "kafka" SET OPTS=!OPTS! -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON
    IF "!conn!" == "grpc " SET OPTS=!OPTS! -DELOG_ENABLE_GRPC_CONNECTOR=ON
)
echo [DEBUG] Parsed connections
echo [DEBUG] Current options: %OPTS%

REM prepare build directory
SET BUILD_DIR=cmake_build\%PLATFORM%-%BUILD_TYPE%
echo INFO: Using build directory: %BUILD_DIR%
IF NOT EXIST %BUILD_DIR% (
    mkdir %BUILD_DIR%
)
IF errorlevel 1 (
    echo ERROR: failed to create build directory %BUILD_DIR%
    GOTO HANDLE_ERROR
)

pushd %BUILD_DIR% > NUL

REM configure phase
echo INFO: Executing build command cmake %OPTS% %* ..\..\
REM TODO: not passing extra parameters after "--"
echo [INFO] Configuring project
cmake %OPTS% ..\..\
IF errorlevel 1 (
    echo ERROR: Configure phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

REM build phase
echo [INFO] Building
cmake --build . -j
IF errorlevel 1 (
    echo ERROR: Build phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

REM install phase
echo [INFO] Installing
cmake --install .
IF errorlevel 1 (
    echo ERROR: Install phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

popd > NUL
exit /b 0

:HANDLE_ERROR
echo Build failed, see errors above, aborting
exit /b 1