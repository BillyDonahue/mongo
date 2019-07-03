#!/bin/bash

#PY3=/home/billy/dev/mongodb/mongo/venv3/bin/python3
PY3=python3
T=build/dynamic_gcc/mongo/bson/bsonobjbuilder_bm

bench_build () {
    $PY3 buildscripts/scons.py \
        MONGO_VERSION=0.0.0 \
        MONGO_GIT_HASH=unknown \
        VARIANT_DIR=dynamic_gcc \
        VERBOSE=1 \
        --allocator=tcmalloc \
        --modules=enterprise \
        --link-model=dynamic \
        --variables-files=etc/scons/mongodbtoolchain_stable_gcc.vars \
        -j 40 \
        "$@"
}

bench_run () {
    sudo cpupower frequency-set --governor performance
    "$1" --benchmark_color=false |tee "$2"
    sudo cpupower frequency-set --governor powersave
}

bench_build $T
bench_run $T before.out
bench_build "CXXFLAGS=-DNUMSTR_STATICS_REMOVED" $T
bench_run $T after.out

$PY3 analyze_bench.py before.out after.out
