#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

REPO="$(git rev-parse --show-toplevel)/src/third_party/gperftools-2.7/dist"

# convenience shell functions
function log2floor () {
  local x=0
  local y=$((($1)>>1))
  while [[ $y -gt 0 ]]; do
    x=$((x+1))
    y=$((y>>1))
  done
  echo $x
}

function set_define () {
    # change any line matching the macro name, surrounded by spaces,
    # to be a #define of that macro to the specified value.
    echo "/ $1 /c\\"
    echo "#define $1 $2"
}

NAME=gperftools
VERSION=2.7
REVISION=$VERSION-mongodb

DEST_DIR=$(git rev-parse --show-toplevel)/src/third_party/$NAME-$VERSION
UNAME=$(uname | tr A-Z a-z)
UNAME_PROCESSOR=$(uname -m)

# Our build system chooses different names in this case so we need to match them
if [[ $UNAME == darwin ]]; then
    UNAME=osx
fi

TARGET_UNAME=${UNAME}_${UNAME_PROCESSOR}

if [[ $UNAME_PROCESSOR == ppc64le ]]; then
    PAGE_SIZE_KB=64
    MAX_SIZE_KB=64
else
    PAGE_SIZE_KB=4
    MAX_SIZE_KB=16
fi

TARGET_TRANSFER_KB=8


HOST_CONFIG=$DEST_DIR/config/$TARGET_UNAME
mkdir -p $HOST_CONFIG
pushd $HOST_CONFIG

CONFIG_CFLAGS="-g -O2 -fno-omit-frame-pointer"

env \
    PATH=/opt/mongodbtoolchain/v3/bin:$PATH \
    CFLAGS="$CONFIG_CFLAGS" \
    CXXFLAGS="$CONFIG_CFLAGS" \
    $REPO/configure \
        --enable-libunwind=no \
        --enable-tcmalloc-aggressive-merge \
        --enable-tcmalloc-mallinfo=no \
        --enable-tcmalloc-unclamped-transfer-sizes=yes \
        --enable-tcmalloc-target-transfer-kb=$TARGET_TRANSFER_KB \
        --with-tcmalloc-pagesize=$PAGE_SIZE_KB \
        --with-tcmalloc-maxsize=$MAX_SIZE_KB
popd

if [[ ! -d $DEST_DIR ]]; then
    WINDOWS_CONFIG=$DEST_DIR/config/windows_x86_64
    mkdir -p "$WINDOWS_CONFIG"
    sed "
    $(set_define TCMALLOC_ENABLE_LIBC_OVERRIDE 0)
    $(set_define TCMALLOC_AGGRESSIVE_MERGE 1)
    $(set_define TCMALLOC_PAGE_SIZE_SHIFT $(log2floor $((PAGE_SIZE_KB*1024))))
    $(set_define TCMALLOC_MAX_SIZE_KB ${MAX_SIZE_KB})
    $(set_define TCMALLOC_TARGET_TRANSFER_KB ${TARGET_TRANSFER_KB})
    $(set_define TCMALLOC_USE_UNCLAMPED_TRANSFER_SIZES 1)
    " \
    < $REPO/src/windows/config.h \
    > $WINDOWS_CONFIG/config.h
fi

#cp src/config.h              $HOST_CONFIG/config.h
#cp src/gperftools/tcmalloc.h $HOST_CONFIG/gperftools/tcmalloc.h

