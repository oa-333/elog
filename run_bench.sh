#!/bin/bash

# change this to actual destination
export INSTALL_DIR=/c/install

# build elog
./build.sh --rel-with-debug-info --verbose
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

# save dev dir
DEV_DIR=`readlink -f .`

# exec benchmark
pushd $INSTALL_DIR/bin

mkdir -p bench_data
rm bench_data/*
cp $INSTALL_DIR/elog/bin/* .
./elog_bench_mingw.exe

# plot
cp $DEV_DIR/src/elog_bench/gnuplot/*.gp .
for x in *.gp
do
    eval ./$x
done

# copy results back
cp ./*.png $DEV_DIR/src/elog_bench/png/

popd