#!/bin/bash

# change this to actual destination
OS=`uname -o`
if [ "$OS" = "Msys" ]; then
    INSTALL_DIR=/c/install/
else
    INSTALL_DIR=~/install/
fi
export INSTALL_DIR

# build elog
./build.sh --rel-with-debug-info --reconfigure --verbose
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
#cp $INSTALL_DIR/elog/bin/* .
#cp $INSTALL_DIR/elog/lib/* .
if [ "$OS" = "Msys" ]; then
    ./elog_bench_mingw.exe
else
    ./elog_bench
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