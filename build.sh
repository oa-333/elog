#!/bin/bash

PLATFORM=$(uname -s)

# Possible options:
# -d|--debug
# -r|--release
# -w|--rel-with-debug-info
# -c|--conn sqlite|mysql|postgresql|kafka
# -i|--install-dir <INSTALL_DIR>
# -l|--clean
# -r|--rebuild (no reconfigure)
# -g|--reconfigure

# set default values
BUILD_TYPE=Debug
OS=`uname -o`
if [ "$OS" = "Msys" ]; then
    INSTALL_DIR=/c/install/elog
else
    INSTALL_DIR=~/install/elog
fi
STACK_TRACE=0
VERBOSE=0
CLEAN=0
REBUILD=0
RE_CONFIG=0
FULL=0

# parse options
TEMP=$(getopt -o vdrwsfc:i:lrg -l verbose,debug,release,rel-with-debug-info,stack-trace,full,conn:,install-dir:,clean,rebuild,reconfigure -- "$@")
eval set -- "$TEMP"

declare -a CONNS=()
while true; do
  case "$1" in
    -v | --verbose ) VERBOSE=1; shift ;;
    -d | --debug ) BUILD_TYPE=Debug; shift ;;
    -r | --release ) BUILD_TYPE=Release; shift ;;
    -w | --rel-with-debug-info ) BUILD_TYPE=RelWithDebInfo; shift ;;
    -s | --stack-trace ) STACK_TRACE=1; shift ;;
    -f | --full ) FULL=1; shift;;
    -c | --conn ) CONNS+=($2); shift 2 ;;
    -i | --install-dir) INSTALL_DIR="$2"; shift 2 ;;
    -l | --clean) CLEAN=1; shift ;;
    -r | --rebuild) REBUILD=1; CLEAN=1; shift ;;
    -g | --reconfigure) RE_CONFIG=1; REBUILD=1; CLEAN=1; shift ;;
    -- ) shift; break ;;
    * ) echo "[ERROR] Invalid option $1, aborting"; exit 1; break ;;
  esac
done

if [ "$FULL" -eq "1" ]; then
    echo "[INFO] Configuring FULL options"
    STACK_TRACE=1
    CONNS=(all)
fi

# set normal options
echo "[INFO] Build type: $BUILD_TYPE"
echo "[INFO] Install dir: $INSTALL_DIR"
echo "[INFO] Stack trace: $STACK_TRACE"
echo "[INFO] Clean: $CLEAN"
echo "[INFO] Rebuild: $REBUILD"
echo "[INFO] Reconfigure: $RE_CONFIG"
OPTS="-DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}"
if [ "$VERBOSE" == "1" ]; then
    OPTS+=" -DCMAKE_VERBOSE_MAKEFILE=ON"
fi
if [ "$STACK_TRACE" == "1" ]; then
    OPTS+=" -DELOG_ENABLE_STACK_TRACE=ON"
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
    elif [ "$conn" == "all" ]; then
        echo "[INFO] Enabling all connectors"
        OPTS+=" -DELOG_ENABLE_SQLITE_DB_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_PGSQL_DB_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_GRPC_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_GRAFANA_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_SENTRY_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_DATADOG_CONNECTOR=ON"
    else
        echo "[ERROR] Invalid connector name $conn, aborting"
        exit 1
    fi
done

# prepare build directory
BUILD_DIR=cmake_build/${PLATFORM}-${BUILD_TYPE}
echo "[INFO] Using build directory: '$BUILD_DIR'"
mkdir -p $BUILD_DIR
pushd $BUILD_DIR > /dev/null

if [ $CLEAN -eq 1 ]; then
    echo "[INFO] Running target clean"
    cmake --build . -j --verbose --target clean
    if [ $? -ne 0 ]; then
        echo "[ERROR] Clean failed, see errors above, aborting"
        popd > /dev/null
        exit 1
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
cmake --build . -j --verbose
if [ $? -ne 0 ]; then
    echo "[ERROR] Build phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

# install phase
echo "[INFO] Installing"
cmake --install .
if [ $? -ne 0 ]; then
    echo "[ERROR] Install phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

popd > /dev/null