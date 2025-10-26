#!/bin/bash

# change this to actual destination
OS=`uname -o`
if [ "$OS" = "Msys" ]; then
    INSTALL_DIR=/c/install/
else
    INSTALL_DIR=~/install/
fi
export INSTALL_DIR

# build elog (enable fmtlib for binary quantum test)
./build.sh --rel-with-debug-info --fmt-lib --reconfigure --verbose
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

# save dev dir
DEV_DIR=`readlink -f .`

# exec benchmark
if [ "$OS" = "Msys" ]; then
    pushd $INSTALL_DIR/bin/Windows_mingw-RelWithDebInfo
else
    pushd $INSTALL_DIR/bin/Linux-RelWithDebInfo
fi

mkdir -p bench_data
rm -f bench_data/*
echo "[INFO] Executing benchmark"
if [ "$OS" = "Msys" ]; then
    echo "[DEBUG] Running command ./elog_bench_mingw.exe $*"
    export ELOG_ENABLE_TIME_SOURCE=yes
    export ELOG_TIME_SOURCE_RESOLUTION=100millis
    ./elog_bench_mingw.exe $*
else
    export LD_LIBRARY_PATH=.
    echo "[DEBUG] Running command ./elog_bench $*"
    ./elog_bench $*
fi

# plot
cp $DEV_DIR/src/elog_bench/gnuplot/*.gp .
for x in *.gp
do
    eval ./$x
done

# copy results back
cp ./*.png $DEV_DIR/src/elog_bench/png/

popd