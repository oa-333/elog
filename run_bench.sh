#!/bin/bash

# change this to actual destination
export INSTALL_DIR=/c/install

# build elog
make -j -f Makefile ELOG_ENABLE_SQLITE_DB_CONNECTOR=1 ELOG_ENABLE_PGSQL_DB_CONNECTOR=1 ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=1 install
if [ $? -ne 0 ]; then
    echo "ERROR: Build failed"
    exit 1
fi

# build bench
make -j -f Makefile ELOG_ENABLE_SQLITE_DB_CONNECTOR=1 ELOG_ENABLE_PGSQL_DB_CONNECTOR=1 ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR=1 bench
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
./elog_bench_mingw.exe

# plot
cp $DEV_DIR/src/elog_bench/gnuplot/*.gp .
for x in *.gp
do
    eval ./$x
done

# copy results back
cp ./*.png $DEV_DIR/sec/elog_bench/png/

popd