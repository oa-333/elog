#!/bin/bash

# change this to actual destination
OS=`uname -o`
if [ "$OS" = "Msys" ]; then
    INSTALL_DIR=/c/install/
else
    INSTALL_DIR=~/install/
fi
export INSTALL_DIR

# build elog (enable all extensions and etcd publishing)
# NOTE: on Windows we test instead for Redis publishing
./build.sh --full --config-publish etcd --doc
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

# save dev dir
DEV_DIR=`readlink -f .`

# exec tests
if [ "$OS" = "Msys" ]; then
    pushd $INSTALL_DIR/bin/Windows_mingw-Debug
else
    pushd $INSTALL_DIR/bin/Linux-Debug
fi

# prepare SQLite db file
rm -f test.db
sqlite3 test.db "create table log_records (rid int64, time varchar(64), level varchar(64), host varchar(64), user varchar(64), prog varchar(64), pid int64, tid int64, mod varchar(64), src varchar(64), msg varchar(1024));"

# set env var used by test
export TEST_ENV_VAR=TEST_ENV_VALUE

mkdir -p test_data
rm -f test_data/*
echo "[INFO] Running tests"
if [ "$OS" = "Msys" ]; then
    echo "[DEBUG] Running command ./elog_test_mingw.exe $*"
    #export ELOG_ENABLE_TIME_SOURCE=yes
    #export ELOG_TIME_SOURCE_RESOLUTION=100millis
    ./elog_test_mingw.exe $*
else
    export LD_LIBRARY_PATH=.
    echo "[DEBUG] Running command ./elog_test $*"
    ./elog_test $*
fi

popd