#!/bin/bash

PLATFORM=$(uname -s)

# Possible options:
# -d|--debug
# -r|--release
# -w|--rel-with-debug-info
# -c|--conn sqlite|mysql|postgresql|kafka
# -i|--install-dir <INSTALL_DIR>

# set default values
BUILD_TYPE=Debug
OS=`uname -o`
if [ "$OS" = "Msys" ]; then
    INSTALL_DIR=/c/install/elog
else
    INSTALL_DIR=~/install/elog
fi

# parse options
TEMP=$(getopt -o vdrwc:i: -l verbose,debug,release,rel-with-debug-info,conn:,install-dir: -- "$@")
eval set -- "$TEMP"

declare -a CONNS=()
while true; do
  case "$1" in
    -v | --verbose ) VERBOSE=1; shift ;;
    -d | --debug ) BUILD_TYPE=Debug; shift ;;
    -r | --release ) BUILD_TYPE=Release; shift ;;
    -w | --rel-with-debug-info ) BUILD_TYPE=RelWithDebInfo; shift ;;
    -c | --conn ) CONNS+=($2); shift 2 ;;
    -i | --install-dir) INSTALL_DIR="$2"; shift 2 ;;
    -- ) shift; break ;;
    * ) echo "[ERROR] Invalid option $1, aborting"; exit 1; break ;;
  esac
done

# set normal options
echo "[INFO] Build type: $BUILD_TYPE"
echo "[INFO] Install dir: $INSTALL_DIR"
OPTS="-DCMAKE_BUILD_TYPE=${BUILD_TYPE} -DCMAKE_INSTALL_PREFIX=${INSTALL_DIR}"
if [ $VERBOSE==1 ]; then
    OPTS+=" -DCMAKE_VERBOSE_MAKEFILE=ON"
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
        OPTS+=" -DELOG_ENABLE_POSTGRESQL_DB_CONNECTOR=ON"
    elif [ "$conn" == "kafka" ]; then
        echo "[INFO] Adding Kafka connector"
        OPTS+=" -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON"
    elif [ "$conn" == "grpc" ]; then
        echo "[INFO] Adding gRPC connector"
        OPTS+=" -DELOG_ENABLE_GRPC_CONNECTOR=ON"
    elif [ "$conn" == "all" ]; then
        echo "[INFO] Enabling all connectors"
        OPTS+=" -DELOG_ENABLE_SQLITE_DB_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_POSTGRESQL_DB_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=ON"
        OPTS+=" -DELOG_ENABLE_GRPC_CONNECTOR=ON"
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

# configure phase
echo "[INFO] Configuring project"
cmake $OPTS $* ../../
if [ $? -ne 0 ]; then
    echo "[ERROR] Configure phase failed, see errors above, aborting"
    popd > /dev/null
    exit 1
fi

# build phase
echo "[INFO] Building target elog"
cmake --build . -j
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