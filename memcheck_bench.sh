#!/bin/bash

pushd cmake_build/Linux-Debug
mkdir -p bench_data
valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    ./elog_bench --perf quantum-private --thread-count 1 --msg-count 10

popd