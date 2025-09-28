@echo off
setlocal enabledelayedexpansion enableextensions

REM Possible command line options:
REM -v|--verbose
REM -d|--debug
REM -r|--release
REM -w|--rel-with-debug-info
REM -e|--secure
REM -x|--cxx-ver
REM -s|--stack-trace
REM -b|--fmt-lib
REM -n|--life-sign
REM -p|--reload-config
REM -f|--full
REM -c|--conn sqlite|mysql|postgresql|kafka
REM -i|--install-dir <INSTALL_DIR>
REM -l|--clean
REM -r|--rebuild (no reconfigure)
REM -g|--reconfigure
REM -m|--mem-check
REM -a|--clang
REM -t|--trace
REM -h|--help

REM set default values
SET PLATFORM=WINDOWS
SET BUILD_TYPE=Debug
SET INSTALL_DIR=C:\install\elog
SET SECURE=0
SET CXX_VER=0
SET STACK_TRACE=0
SET FMT_LIB=0
SET LIFE_SIGN=0
SET RELOAD_CONFIG=0
SET VERBOSE=0
SET FULL=0
SET CLEAN=0
SET REBUILD=0
SET RE_CONFIG=0
SET MEM_CHECK=0
SET CLANG=0
SET TRACE=0
SET HELP=0

SET CONN_INDEX=0
SET CONNS[0]=tmp
REM echo [DEBUG] Parsing args
:GET_OPTS
REM Remove extra spaces from arguments
SET ARG1=%1
SET ARG1=%ARG1: =%
SET ARG2=%2
SET ARG2=%ARG2: =%
REM echo [DEBUG] processing option "%ARG1%" "%ARG2%"
IF /I "%ARG1%" == "-v" SET VERBOSE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--verbose" SET VERBOSE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-d" SET BUILD_TYPE=Debug & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--debug" SET BUILD_TYPE=Debug & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-r" SET BUILD_TYPE=Release & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--release" SET BUILD_TYPE=Release & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-w" SET BUILD_TYPE=RelWithDebInfo & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--rel-with-debug-info" SET BUILD_TYPE=RelWithDebInfo & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-e" SET SECURE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--secure" SET SECURE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-x" SET CXX_VER=%ARG2% & shift & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--cxx-ver" SET CXX_VER=%ARG2% & shift & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-s" SET STACK_TRACE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--stack-trace" SET STACK_TRACE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-b" SET FMT_LIB=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--fmt-lib" SET FMT_LIB=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-n" SET LIFE_SIGN=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--life-sign" SET LIFE_SIGN=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-p" SET RELOAD_CONFIG=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--reload-config" SET RELOAD_CONFIG=1 & GOTO CHECK_OPTS
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
IF /I "%ARG1%" == "-m" SET MEM_CHECK=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--mem-check" SET MEM_CHECK=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-a" SET CLANG=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--clang" SET CLANG=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-t" SET TRACE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--trace" SET TRACE=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "-h" SET HELP=1 & GOTO CHECK_OPTS
IF /I "%ARG1%" == "--help" SET HELP=1 & GOTO CHECK_OPTS
REM echo [DEBUG] "%ARG1%" Not matched

REM handle invalid option
IF NOT "%1" == "" (
    echo [ERROR] Invalid option: "%1"
    GOTO HANDLE_ERROR
)

:CHECK_OPTS
shift
IF "%1" == "--" shift & GOTO SET_OPTS
IF NOT "%1" == "" GOTO GET_OPTS

IF %HELP% EQU 1 GOTO PRINT_HELP

:SET_OPTS
echo [DEBUG] Parsed args:
echo [DEBUG] BUILD_TYPE=%BUILD_TYPE%
echo [DEBUG] INSTALL_DIR=%INSTALL_DIR%
echo [DEBUG] SECURE=%SECURE%
echo [DEBUG] CXX_VER=%CXX_VER%
echo [DEBUG] STACK_TRACE=%STACK_TRACE%
echo [DEBUG] FMT_LIB=%FMT_LIB%
echo [DEBUG] LIFE_SIGN=%LIFE_SIGN%
echo [DEBUG] RELOAD_CONFIG=%RELOAD_CONFIG%
echo [DEBUG] VERBOSE=%VERBOSE%
echo [DEBUG] FULL=%FULL%
echo [DEBUG] CLEAN=%CLEAN%
echo [DEBUG] REBUILD=%REBUILD%
echo [DEBUG] RE_CONFIG=%RE_CONFIG%
echo [DEBUG] MEM_CHECK=%MEM_CHECK%
echo [DEBUG] CLANG=%CLANG%
echo [DEBUG] TRACE=%TRACE%
echo [DEBUG] Args parsed, options left: %*

IF %FULL% EQU 1 (
    echo [INFO]  Configuring FULL options
    SET SECURE=1
    SET STACK_TRACE=1
    SET FMT_LIB=1
    SET LIFE_SIGN=1
    SET RELOAD_CONFIG=1
    SET CONNS[0]=all
    SET CONN_INDEX=1
)

REM set options
SET OPTS=-DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_INSTALL_PREFIX=%INSTALL_DIR%
IF %VERBOSE% EQU 1 SET OPTS=%OPTS% -DCMAKE_VERBOSE_MAKEFILE=ON
IF %SECURE% EQU 1 SET OPTS=%OPTS% -DELOG_SECURE=ON
IF %STACK_TRACE% EQU 1 SET OPTS=%OPTS% -DELOG_ENABLE_STACK_TRACE=ON
IF %FMT_LIB% EQU 1 (
    SET OPTS=%OPTS% -DELOG_ENABLE_FMT_LIB=ON
    vcpkg add port fmt
)
IF %LIFE_SIGN% EQU 1 SET OPTS=%OPTS% -DELOG_ENABLE_LIFE_SIGN=ON
IF %RELOAD_CONFIG% EQU 1 SET OPTS=%OPTS% -DELOG_ENABLE_RELOAD_CONFIG=ON
IF %MEM_CHECK% EQU 1 SET OPTS=%OPTS% -DELOG_ENABLE_MEM_CHECK=ON
IF %TRACE% EQU 1 SET OPTS=%OPTS% -DELOG_ENABLE_GROUP_FLUSH_GC_TRACE=ON
IF %CLANG% EQU 1 (
    SET CXX=clang-cl
    IF %CXX_VER% EQU 0 SET CXX_VER=23
) ELSE (
    IF %CXX_VER% EQU 0 SET CXX_VER=20
)
echo [DEBUG] CXX_VER=%CXX_VER%
SET OPTS=%OPTS% -DCMAKE_CXX_STANDARD=%CXX_VER%
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
        vcpkg add port gzip-hpp
        vcpkg add port zlib
    )
    IF "!conn!" == "net" (
        SET OPTS=!OPTS! -DELOG_ENABLE_NET=ON
    )
    IF "!conn!" == "ipc" (
        SET OPTS=!OPTS! -DELOG_ENABLE_IPC=ON
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
        SET OPTS=!OPTS! -DELOG_ENABLE_NET=ON
        SET OPTS=!OPTS! -DELOG_ENABLE_IPC=ON
        vcpkg add port sqlite3
        vcpkg add port libpqxx
        vcpkg add port librdkafka
        vcpkg add port grpc
        vcpkg add port cpp-httplib
        vcpkg add port nlohmann-json
        vcpkg add port sentry-native
        vcpkg add port gzip-hpp
        vcpkg add port zlib
    )
)
REM echo [DEBUG] Parsed connections
REM echo [DEBUG] Current options: %OPTS%
REM echo [DEBUG] MYSQL_ROOT=%MYSQL_ROOT%

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

if %VERBOSE% EQU 1 (
    SET VERBOSE_OPT=--verbose
)

IF %CLEAN% EQU 1 (
    echo [INFO] Running target clean
    cmake --build . -j %VERBOSE_OPT% --target clean
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
echo [INFO]  Executing configure command cmake --preset=default %OPTS% -S ..\..\ -B . -G "Ninja"
cmake --preset=default %OPTS% -S ..\..\ -B . -G "Ninja"
IF errorlevel 1 (
    echo [ERROR] Configure phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

REM build phase
REM NOTE: On windows MSVC (multi-config system), then configuration to build should be specified in build command
echo [INFO]  Building
echo [INFO] Executing build command: cmake --build . -j %VERBOSE_OPT% --config %BUILD_TYPE%
cmake --build . -j %VERBOSE_OPT% --config %BUILD_TYPE%
IF errorlevel 1 (
    echo [ERROR] Build phase failed, see errors above, aborting
    popd > NUL
    GOTO HANDLE_ERROR
)

REM install phase
echo [INFO]  Installing
cmake --install . %VERBOSE_OPT% --config %BUILD_TYPE%
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

:PRINT_HELP
echo.
echo ELog build script syntax:
echo.
echo build.bat [BUILD MODE] [EXTENSIONS] [BUILD OPTIONS] [DEBUG OPTIONS] [MISC OPTIONS]
echo.
echo.
echo BUILD MODE OPTIONS
echo.
echo       -r^|--release                Build in release mode.
echo       -d^|--debug                  Build in debug mode.
echo       -w^|--rel-with-debug-info    Build in release mode with debug symbols.
echo       -e^|--secure                 Uses secure C runtime functions.
echo       -x^|--cxx-ver VERSION        C++ version (11, 14, 17 or 20, default is 20)."
echo.
echo If none is specified, then the default is debug build mode.
echo.
echo.
echo EXTENSIONS OPTIONS
echo.
echo       -c^|--conn CONNECTOR_NAME    Enables connector.
echo       -s^|--stack-trace            Enables stack trace logging API.
echo       -b^|--fmt-lib                Enables fmtlib formatting style support.
echo       -n^|--life-sign              Enables periodic life-sign reports.
echo       -p^|--reload-config          Enables periodic configuration reloading.
echo       -f^|--full                   Enables all connectors and stack trace logging API.
echo.
echo By default no connector is enabled, and stack trace logging is disabled.
echo The following connectors are currently supported:
echo.
echo   Name            Connector
echo   ----            ---------
echo   grafana         Grafana-Loki connector
echo   sentry          Sentry connector
echo   datadog         Datadog connector
echo   sqlite          SQLite database connector
echo   mysql           MySQL database connector (experimental)
echo   postgresql      PostgreSQL database connector
echo   kafka           Kafka topic connector
echo   grpc            gRPC connector
echo   net             Network (TCP/UDP) connector
echo   ipc             IPC (Unix Domain Sockets/Windows Pipes) connector
echo   all             Enables all connectors
echo.
echo.
echo BUILD OPTIONS
echo.
echo       -v^|--verbose        Issue verbose messages during build
echo       -l^|--clean          Cleans previous build
echo       -g^|--reconfigure    Forces rerunning configuration phase of CMake.
echo       -a^|--clang          Use clang toolchain for builder, rather than default gcc.
echo       -i^|--install-dir INSTALL_PATH   Specifies installation directory.
echo.
echo.
echo DEBUG OPTIONS
echo.
echo       -t^|--trace          Enable trace logging of some components.
echo       -m^|--mem-check      Enables address sanitizers to perform memory checks.
echo.
echo.
echo MISC OPTIONS
echo.
echo       -h^|--help           Prints this help screen.
exit /b 0