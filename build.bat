@echo off
setlocal enabledelayedexpansion enableextensions

REM Possible command line options:
REM -v|--verbose
REM -d|--debug
REM -r|--release
REM -w|--rel-with-debug-info
REM -s|--stack-trace
REM -f|--full
REM -c|--conn sqlite|mysql|postgresql|kafka
REM -i|--install-dir <INSTALL_DIR>
REM -l|--clean
REM -r|--rebuild (no reconfigure)
REM -g|--reconfigure

REM set default values
SET PLATFORM=WINDOWS
SET BUILD_TYPE=Debug
SET INSTALL_DIR=C:\install\elog
SET STACK_TRACE=0
SET VERBOSE=0
SET FULL=0
SET CLEAN=0
SET REBUILD=0
SET RE_CONFIG=0

SET CONN_INDEX=0
SET CONNS[0]=tmp
echo [DEBUG] Parsing args
:GET_OPTS
SET ARG1=%1
SET ARG1=%ARG1: =%
SET ARG2=%2
SET ARG2=%ARG2: =%
echo [DEBUG] processing option "%ARG1%" "%ARG2%"
IF /I "%ARG1%" == "-v" SET VERBOSE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--verbose" SET VERBOSE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-d" SET BUILD_TYPE=Debug & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--debug" SET BUILD_TYPE=Debug & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-r" SET BUILD_TYPE=Release & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--release" SET BUILD_TYPE=Release & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-w" SET BUILD_TYPE=RelWithDebInfo & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--rel-with-debug-info" SET BUILD_TYPE=RelWithDebInfo & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-s" SET STACK_TRACE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--stack-trace" SET STACK_TRACE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-f" SET FULL=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--full" SET FULL=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-c" SET CONNS[!CONN_INDEX!]=%ARG2% & SET /A CONN_INDEX+=1 & shift & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--conn" SET CONNS[!CONN_INDEX!]=%ARG2% & SET /A CONN_INDEX+=1 & shift & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-i" SET INSTALL_DIR=%ARG2% & shift & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--install-dir" SET INSTALL_DIR=%ARG2% & shift & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-l" SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--clean" SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-r" SET REBUILD=1 & SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--rebuild" SET REBUILD=1 & SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-g" SET RE_CONFIG=1 & SET REBUILD=1 & SET CLEAN=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--reconfigure" SET RE_CONFIG=1 & SET REBUILD=1 & SET CLEAN=1 & GOTO CHECK_OPTS

REM handle invalid option
IF NOT "%1" == "" (
    echo [ERROR] Invalid option: "%1"
    GOTO HANDLE_ERROR
)

:CHECK_OPTS
shift
IF "%1" == "--" shift & GOTO SET_OPTS
IF NOT "%1" == "" GOTO GET_OPTS

:SET_OPTS
echo [DEBUG] Parsed args:
echo [DEBUG] BUILD_TYPE=%BUILD_TYPE%
echo [DEBUG] INSTALL_DIR=%INSTALL_DIR%
echo [DEBUG] STACK_TRACE=%STACK_TRACE%
echo [DEBUG] VERBOSE=%VERBOSE%
echo [DEBUG] FULL=%FULL%
echo [DEBUG] CLEAN=%CLEAN%
echo [DEBUG] REBUILD=%REBUILD%
echo [DEBUG] RE_CONFIG=%RE_CONFIG%
echo [DEBUG] Args parsed, options left: %*

IF %FULL% EQU 1 (
    echo [INFO]  Configuring FULL options
    SET STACK_TRACE=1
    SET CONNS[0]=all
    SET CONN_INDEX=1
)

REM set normal options
echo [INFO]  Build type: %BUILD_TYPE%
echo [INFO]  Install dir: %INSTALL_DIR%
SET OPTS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%
IF %VERBOSE% EQU 1 SET OPTS=%OPTS% -DCMAKE_VERBOSE_MAKEFILE=ON
IF %STACK_TRACE% EQU 1 SET OPTS=%OPTS% -DELOG_ENABLE_STACK_TRACE=ON
echo [DEBUG] Current options: %OPTS%

REM add optional connectors
REM TODO: this is not working, for some reason the connector name has extra trailing whitespace
echo [DEBUG] Parsing %CONN_INDEX% connectors
set /A CONN_COUNT=%CONN_INDEX-1
for /l %%n in (0,1,%CONN_COUNT%) do (
    set conn=!CONNS[%%n]!
    set conn=!conn: =!
    echo [DEBUG] Adding connector --!conn!-- to OPTS %OPTS%
    IF "!conn!" == "sqlite" (
        SET OPTS=!OPTS! -DELOG_ENABLE_SQLITE_DB_CONNECTOR=ON
        vcpkg add port sqlite3
    )
    IF "!conn!" == "mysql" (
        IF "%MYSQL_ROOT%" == "" SET MYSQL_ROOT="C:\\Program Files\\MySQL\\MySQL Connector C++ 9.3"
        SET OPTS=!OPTS! -DELOG_ENABLE_MYSQL_DB_CONNECTOR=ON -DMYSQL_ROOT=!MYSQL_ROOT!
        REM NOTE: usage of vcpkg for MySQL is still not working well, see CMakeLists.txt for more details
        REM vcpkg add port mysql-connector-cpp
    )
    IF "!conn!" == "postgresql" (
        SET OPTS=!OPTS! -DELOG_ENABLE_PGSQL_DB_CONNECTOR=ON
        vcpkg add port libpqxx
    )
    IF "!conn!" == "kafka" (
        SET OPTS=!OPTS! -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON
        vcpkg add port librdkafka
    )
    IF "!conn!" == "grpc" (
        SET OPTS=!OPTS! -DELOG_ENABLE_GRPC_CONNECTOR=ON
        vcpkg add port grpc
    )
    IF "!conn!" == "grafana" (
        SET OPTS=!OPTS! -DELOG_ENABLE_GRAFANA_CONNECTOR=ON
        vcpkg add port cpp-httplib
        vcpkg add port nlohmann-json
    )
    IF "!conn!" == "sentry" (
        SET OPTS=!OPTS! -DELOG_ENABLE_SENTRY_CONNECTOR=ON
        vcpkg add port sentry-native
    )
    IF "!conn!" == "datadog" (
        SET OPTS=!OPTS! -DELOG_ENABLE_DATADOG_CONNECTOR=ON
        vcpkg add port cpp-httplib
        vcpkg add port nlohmann-json
    )
    IF "!conn!" == "all" (
        echo [INFO]  Enabling all connectors
        IF "%MYSQL_ROOT%" == "" SET MYSQL_ROOT="C:\\Program Files\\MySQL\\MySQL Connector C++ 9.3"
        SET OPTS=!OPTS! -DELOG_ENABLE_SQLITE_DB_CONNECTOR=ON
        SET OPTS=!OPTS! -DELOG_ENABLE_MYSQL_DB_CONNECTOR=ON -DMYSQL_ROOT=!MYSQL_ROOT!
        SET OPTS=!OPTS! -DELOG_ENABLE_PGSQL_DB_CONNECTOR=ON
        SET OPTS=!OPTS! -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON
        SET OPTS=!OPTS! -DELOG_ENABLE_GRPC_CONNECTOR=ON
        SET OPTS=!OPTS! -DELOG_ENABLE_GRAFANA_CONNECTOR=ON
        SET OPTS=!OPTS! -DELOG_ENABLE_SENTRY_CONNECTOR=ON
        SET OPTS=!OPTS! -DELOG_ENABLE_DATADOG_CONNECTOR=ON
        vcpkg add port sqlite3
        vcpkg add port libpqxx
        vcpkg add port librdkafka
        vcpkg add port grpc
        vcpkg add port cpp-httplib
        vcpkg add port nlohmann-json
        vcpkg add port sentry-native
    )
)
echo [DEBUG] Parsed connections
echo [DEBUG] Current options: %OPTS%
echo [DEBUG] MYSQL_ROOT=%MYSQL_ROOT%

REM prepare build directory
SET BUILD_DIR=cmake_build\%PLATFORM%-%BUILD_TYPE%
echo [INFO]  Using build directory: %BUILD_DIR%
IF NOT EXIST %BUILD_DIR% mkdir %BUILD_DIR%
IF errorlevel 1 (
    echo [ERROR] failed to create build directory %BUILD_DIR%
    GOTO HANDLE_ERROR
)

pushd %BUILD_DIR% > NUL

REM print cmake info
echo [INFO] CMake version:
cmake --version

IF %CLEAN% EQU 1 (
    echo [INFO] Running target clean
    cmake --build . -j --verbose --target clean
    if errorlevel 1 (
        echo [ERROR] Clean failed, see errors above, aborting
        popd > NUL
        GOTO HANDLE_ERROR
    )
    echo [INFO] Clean DONE
    IF %REBUILD% EQU 0 (
        popd > NUL
        exit /b 0
    )
)

REM Run fresh if re-configure is requested
IF %RE_CONFIG% EQU 1 (
    echo [INFO] Forcing fresh configuration
    SET OPTS=--fresh %OPTS%
)

REM configure phase
REM TODO: not passing extra parameters after "--"
echo [INFO] VCPKG_ROOT=%VCPKG_ROOT%
echo [INFO]  Configuring project
REM NOTE: when vcpkg is used, it seems that cmake default to MSBuild which is very slow, so we force Ninja generator
echo [INFO]  Executing build command cmake --preset=default %OPTS% -S ..\..\ -B . -G "Ninja"
cmake --preset=default %OPTS% -S ..\..\ -B . -G "Ninja"
IF errorlevel 1 (
    echo [ERROR] Configure phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

REM build phase
REM NOTE: On windows MSVC (multi-config system), then configuration to build should be specified in build command
echo [INFO]  Building
echo [INFO] Executing command: cmake --build . -j --verbose --config %BUILD_TYPE%
cmake --build . -j --verbose --config %BUILD_TYPE%
IF errorlevel 1 (
    echo [ERROR] Build phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

REM install phase
echo [INFO]  Installing
cmake --install . --verbose --config %BUILD_TYPE%
IF errorlevel 1 (
    echo [ERROR] Install phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

popd > NUL
exit /b 0

:HANDLE_ERROR
echo Build failed, see errors above, aborting
exit /b 1