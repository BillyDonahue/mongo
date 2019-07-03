#!/bin/bash

PY3=python3
T=build/dynamic_gcc/mongo/bson/bsonobjbuilder_bm

set -e

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


bench_git () {
    git checkout $2 src/mongo/bson
    bench_build $1
    bench_run $1 $3
}

bench_git $T itoa/1 before.out
bench_git $T itoa/2 after.out

$PY3 analyze_bench.py before.out after.out
