#!/bin/bash

# Possible options:
# -v|--verbose
# -d|--debug
# -r|--release
# -w|--rel-with-debug-info
# -e|--secure
# -x|--cxx-ver
# -s|--stack-trace
# -b|--fmt-lib
# -n|--life-sign
# -p|--reload-config
# -f|--full
# -c|--conn sqlite|mysql|postgresql|kafka
# -i|--install-dir <INSTALL_DIR>
# -l|--clean
# -r|--rebuild (no reconfigure)
# -g|--reconfigure
# -m|--mem-check
# -a|--clang
# -t|--trace
# -h|--help

# set default values
PLATFORM=$(uname -s)
BUILD_TYPE=Debug
OS=`uname -o`
if [ "$OS" = "Msys" ]; then
    INSTALL_DIR=/c/install/elog
else
    INSTALL_DIR=~/install/elog
fi
SECURE=0
CXX_VER=23
STACK_TRACE=0
FMT_LIB=0
LIFE_SIGN=0
RELOAD_CONFIG=0
VERBOSE=0
FULL=0
CLEAN=0
REBUILD=0
RE_CONFIG=0
MEM_CHECK=0
CLANG=0
TRACE=0
HELP=0

# parse options
TEMP=$(getopt -o vdrwexsbnpfc:i:lrgmath -l verbose,debug,release,rel-with-debug-info,secure,cxx-ver:,stack-trace,fmt-lib,life-sign,reload-config,full,conn:,install-dir:,clean,rebuild,reconfigure,mem-check,clang,trace,help -- "$@")
eval set -- "$TEMP"

declare -a CONNS=()
while true; do
  case "$1" in
    -v | --verbose ) VERBOSE=1; shift ;;
    -d | --debug ) BUILD_TYPE=Debug; shift ;;
    -r | --release ) BUILD_TYPE=Release; shift ;;
    -w | --rel-with-debug-info ) BUILD_TYPE=RelWithDebInfo; shift ;;
    -e | --secure) SECURE=1; shift ;;
    -x | --cxx-ver) CXX_VER=$2; shift 2 ;;
    -s | --stack-trace ) STACK_TRACE=1; shift ;;
    -b | --fmt-lib ) FMT_LIB=1; shift ;;
    -n | --life-sign ) LIFE_SIGN=1; shift ;;
    -p | --reload-config ) RELOAD_CONFIG=1; shift ;;
    -f | --full ) FULL=1; shift;;
    -c | --conn ) CONNS+=($2); shift 2 ;;
    -i | --install-dir) INSTALL_DIR="$2"; shift 2 ;;
    -l | --clean) CLEAN=1; shift ;;
    -r | --rebuild) REBUILD=1; CLEAN=1; shift ;;
    -g | --reconfigure) RE_CONFIG=1; REBUILD=1; CLEAN=1; shift ;;
    -m | --mem-check) MEM_CHECK=1; shift ;;
    -a | --clang) CLANG=1; shift ;;
    -t | --trace) TRACE=1; shift ;;
    -h | --help) HELP=1; shift ;;
    -- ) shift; break ;;
    * ) echo "[ERROR] Invalid option $1, aborting"; exit 1; break ;;
  esac
done

if [ "$HELP" -eq "1" ]; then
    echo ""
    echo "build.sh [BUILD MODE] [EXTENSIONS] [BUILD OPTIONS] [DEBUG OPTIONS] [MISC OPTIONS]"
    echo ""
    echo ""
    echo "BUILD MODE OPTIONS"
    echo ""
    echo "      -r|--release                Build in release mode."
    echo "      -d|--debug                  Build in debug mode."
    echo "      -w|--rel-with-debug-info    Build in release mode with debug symbols."
    echo "      -e|--secure                 Uses secure C runtime functions."
    echo "      -x|--cxx-ver VERSION        C++ version (11, 14, 17, 20 or 23, default is 23)."
    echo ""
    echo "If none is specified, then the default is debug build mode."
    echo ""
    echo ""
    echo "EXTENSIONS OPTIONS"
    echo ""
    echo "      -c|--conn CONNECTOR_NAME    Enables connector."
    echo "      -s|--stack-trace            Enables stack trace logging API."
    echo "      -b|--fmt-lib                Enables fmtlib formatting style support."
    echo "      -n|--life-sign              Enables periodic life-sign reports."
    echo "      -p|--reload-config          Enables periodic configuration reloading."
    echo "      -f|--full                   Enables all connectors and stack trace logging API."
    echo ""
    echo "By default no connector is enabled, and stack trace logging is disabled."
    echo "The following connectors are currently supported:"
    echo "  Name            Connector"
    echo "  ----            ---------"
    echo "  grafana         Grafana-Loki connector"
    echo "  sentry          Sentry connector"
    echo "  datadog         Datadog connector"
    echo "  otel            Open Telemetry connector"
    echo "  sqlite          SQLite database connector"
    echo "  mysql           MySQL database connector (experimental)"
    echo "  postgresql      PostgreSQL database connector"
    echo "  redis           Redis database connector"
    echo "  kafka           Kafka topic connector"
    echo "  grpc            gRPC connector"
    echo "  net             Network (TCP/UDP) connector"
    echo "  ipc             IPC (Unix Domain Sockets/Windows Pipes) connector"
    echo "  all             Enables all connectors"
    echo ""
    echo ""
    echo "BUILD OPTIONS"
    echo ""
    echo "      -v|--verbose        Issue verbose messages during build"
    echo "      -l|--clean          Cleans previous build"
    echo "      -g|--reconfigure    Forces rerunning configuration phase of CMake."
    echo "      -a|--clang          Use clang toolchain for builder, rather than default gcc."
    echo "      -i|--install-dir INSTALL_PATH   Specifies installation directory."
    echo ""
    echo ""
    echo "DEBUG OPTIONS"
    echo ""
    echo "      -t|--trace          Enable trace logging of some components."
    echo "      -m|--mem-check      Enables address sanitizers to perform memory checks."
    echo ""
    echo ""
    echo "MISC OPTIONS"
    echo ""
    echo "      -h|--help           Prints this help screen."
    exit 0
fi

# set normal options
echo "[INFO] Build type: $BUILD_TYPE"
echo "[INFO] Install dir: $INSTALL_DIR"
echo "[INFO] Secure: $SECURE"
echo "[INFO] CXX Version: $CXX_VER"
echo "[INFO] Stack trace: $STACK_TRACE"
echo "[INFO] fmtlib: $FMT_LIB"
echo "[INFO] Life sign: $LIFE_SIGN"
echo "[INFO] Reload config: $RELOAD_CONFIG"
echo "[INFO] Verbose: $VERBOSE"
echo "[INFO] Full: $FULL"
echo "[INFO] Clean: $CLEAN"
echo "[INFO] Rebuild: $REBUILD"
echo "[INFO] Reconfigure: $RE_CONFIG"
echo "[INFO] Mem-check: $MEM_CHECK"
echo "[INFO] Clang: $CLANG"

if [ "$FULL" -eq "1" ]; then
    echo "[INFO] Configuring FULL options"
    SECURE=1
    STACK_TRACE=1
    FMT_LIB=1
    LIFE_SIGN=1
    RELOAD_CONFIG=1
    CONNS=(all)
fi

# set options
OPTS="-DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}"
if [ "$VERBOSE" == "1" ]; then
    OPTS+=" -DCMAKE_VERBOSE_MAKEFILE=ON"
fi
if [ "$SECURE" == "1" ]; then
    OPTS+=" -DELOG_SECURE=ON"
fi
OPTS+=" -DCMAKE_CXX_STANDARD=$CXX_VER"
if [ "$STACK_TRACE" == "1" ]; then
    OPTS+=" -DELOG_ENABLE_STACK_TRACE=ON"
fi
if [ "$FMT_LIB" == "1" ]; then
    OPTS+=" -DELOG_ENABLE_FMT_LIB=ON"
fi
if [ "$LIFE_SIGN" == "1" ]; then
    OPTS+=" -DELOG_ENABLE_LIFE_SIGN=ON"
fi
if [ "$RELOAD_CONFIG" == "1" ]; then
    OPTS+=" -DELOG_ENABLE_RELOAD_CONFIG=ON"
fi
if [ "$MEM_CHECK" == "1" ]; then
    OPTS+=" -DELOG_ENABLE_MEM_CHECK=ON"
fi
if [ "$TRACE" == "1" ]; then
    OPTS+=" -DELOG_ENABLE_GROUP_FLUSH_GC_TRACE=ON"
fi
if [ "$CLANG" == "1" ]; then
    export CXX=`which clang++`;
    if [ -z "$CXX" ]; then
        echo "[ERROR] clang not found, aborting"
        exit 1
    fi
fi

# add optional connectors
for conn in "${CONNS[@]}"
do
    if [ "$conn" == "sqlite" ]; then
        echo "[INFO] Adding SQLite connector"
        OPTS+=" -DELOG_ENABLE_SQLITE_DB_CONNECTOR=ON"
    elif [ "$conn" == "mysql" ]; then
        echo "[INFO] Adding MySQL connector"
        OPTS+=" -DELOG_ENABLE_MYSQL_DB_CONNECTOR=ON"
    elif [ "$conn" == "postgresql" ]; then
        echo "[INFO] Adding PostgreSQL connector"
        OPTS+=" -DELOG_ENABLE_PGSQL_DB_CONNECTOR=ON"
    elif [ "$conn" == "redis" ]; then
        echo "[INFO] Adding Redis connector"
        OPTS+=" -DELOG_ENABLE_REDIS_DB_CONNECTOR=ON"
    elif [ "$conn" == "kafka" ]; then
        echo "[INFO] Adding Kafka connector"
        OPTS+=" -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON"
    elif [ "$conn" == "grpc" ]; then
        echo "[INFO] Adding gRPC connector"
        OPTS+=" -DELOG_ENABLE_GRPC_CONNECTOR=ON"
    elif [ "$conn" == "grafana" ]; then
        echo "[INFO] Adding Grafana connector"
        OPTS+=" -DELOG_ENABLE_GRAFANA_CONNECTOR=ON"
    elif [ "$conn" == "sentry" ]; then
        echo "[INFO] Adding Sentry connector"
        OPTS+=" -DELOG_ENABLE_SENTRY_CONNECTOR=ON"
    elif [ "$conn" == "datadog" ]; then
        echo "[INFO] Adding Datadog connector"
        OPTS+=" -DELOG_ENABLE_DATADOG_CONNECTOR=ON"
    elif [ "$conn" == "otel" ]; then
        echo "[INFO] Adding Open Telemetry connector"
        OPTS+=" -DELOG_ENABLE_OTEL_CONNECTOR=ON"
    elif [ "$conn" == "net" ]; then
        echo "[INFO] Adding Network connector"
        OPTS+=" -DELOG_ENABLE_NET=ON"
    elif [ "$conn" == "ipc" ]; then
        echo "[INFO] Adding IPC connector"
        OPTS+=" -DELOG_ENABLE_IPC=ON"
    elif [ "$conn" == "all" ]; then
        echo "[INFO] Enabling all connectors"
        OPTS+=" -DELOG_ENABLE_SQLITE_DB_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_PGSQL_DB_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_GRPC_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_GRAFANA_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_SENTRY_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_DATADOG_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_OTEL_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_NET=ON"
        OPTS+=" -DELOG_ENABLE_IPC=ON"
    else
        echo "[ERROR] Invalid connector name $conn, aborting"
        exit 1
    fi
done

if [ $VERBOSE -eq 1 ]; then
    VERBOSE_OPT=--verbose
fi

# prepare build directory
BUILD_DIR=cmake_build/${PLATFORM}-${BUILD_TYPE}
echo "[INFO] Using build directory: '$BUILD_DIR'"
mkdir -p $BUILD_DIR
pushd $BUILD_DIR > /dev/null

if [ $CLEAN -eq 1 ]; then
    echo "[INFO] Running target clean"
    cmake --build . -j $VERBOSE_OPT --target clean
    if [ $? -ne 0 ]; then
        echo "[WARN] Clean failed, see errors above, build continues"
        # popd > /dev/null
        # exit 1
    fi
    if [ $REBUILD -eq 0 ]; then
        popd > /dev/null
        exit 0
    fi
fi

# Run fresh if re-configure is requested
if [ $RE_CONFIG -eq 1 ]; then 
    echo "[INFO] Forcing fresh configuration"
    OPTS="--fresh $OPTS"
fi

# configure phase
echo "[INFO] Configuring project"
echo "[INFO] Using options: $OPTS"
cmake $OPTS $* ../../
if [ $? -ne 0 ]; then
    echo "[ERROR] Configure phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

# build phase
echo "[INFO] Building target elog"
cmake --build . -j $VERBOSE_OPT
if [ $? -ne 0 ]; then
    echo "[ERROR] Build phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

# install phase
echo "[INFO] Installing"
cmake --install . $VERBOSE_OPT
if [ $? -ne 0 ]; then
    echo "[ERROR] Install phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

popd > /dev/null