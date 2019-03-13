#!/bin/bash

set -euo pipefail
IFS=$'\n\t'

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
MACOSX_VERSION_MIN=10.12

DEST_DIR=$(git rev-parse --show-toplevel)/src/third_party/$NAME-$VERSION

UNAME=$(uname | tr A-Z a-z)
UNAME_PROCESSOR=$(uname -m)

# Our build system chooses different names in this case so we need to match them
if [[ $UNAME == darwin ]]; then
    UNAME=osx
fi

TARGET_UNAME=${UNAME}_${UNAME_PROCESSOR}

PAGE_SIZE_KB=4
MAX_SIZE_KB=16
TARGET_TRANSFER_KB=8

WINDOWS_PAGE_SIZE_KB=$PAGE_SIZE_KB
WINDOWS_MAX_SIZE_KB=$MAX_SIZE_KB
WINDOWS_TARGET_TRANSFER_KB=$TARGET_TRANSFER_KB

if [[ $UNAME_PROCESSOR == ppc64le ]]; then
    PAGE_SIZE_KB=64
    MAX_SIZE_KB=64
fi

HOST_CONFIG=$DEST_DIR/config/$TARGET_UNAME
mkdir -p $HOST_CONFIG
pushd $HOST_CONFIG

COMMON_FLAGS="-g -O2"   # configure's defaults for both C and C++

if [[ $UNAME == linux ]]; then
    ENV_CPPFLAGS="CPPFLAGS=-D_XOPEN_SOURCE=700 -D_GNU_SOURCE"
elif [[ $UNAME == osx ]]; then
    COMMON_FLAGS+=" -mmacosx-version-min=$MACOSX_VERSION_MIN"
fi

ENV_CFLAGS="CFLAGS=$COMMON_FLAGS"
ENV_CXXFLAGS="CXXFLAGS=$COMMON_FLAGS -std=c++17"

env \
    PATH=/opt/mongodbtoolchain/v3/bin:$PATH \
    ${ENV_CPPFLAGS:-} \
    ${ENV_CFLAGS:-} \
    ${ENV_CXXFLAGS:-} \
    $DEST_DIR/dist/configure \
        --enable-frame-pointers=yes \
        --enable-libunwind=no \
        --enable-sized-delete=yes \
        --enable-tcmalloc-aggressive-merge \
        --enable-tcmalloc-mallinfo=no \
        --enable-tcmalloc-unclamped-transfer-sizes=yes \
        --enable-tcmalloc-target-transfer-kb=$TARGET_TRANSFER_KB \
        --with-tcmalloc-pagesize=$PAGE_SIZE_KB \
        --with-tcmalloc-maxsize=$MAX_SIZE_KB
popd

# Pseudo-configure Windows.
# Editing with sed is the best we can do, as gperftools doesn't
# ship with a Windows configuration mechanism.
WINDOWS_CONFIG=$DEST_DIR/config/windows_x86_64
mkdir -p "$WINDOWS_CONFIG/src"
sed "
$(set_define TCMALLOC_ENABLE_LIBC_OVERRIDE 0)
$(set_define TCMALLOC_AGGRESSIVE_MERGE 1)
$(set_define TCMALLOC_PAGE_SIZE_SHIFT $(log2floor $((WINDOWS_PAGE_SIZE_KB*1024))))
$(set_define TCMALLOC_MAX_SIZE_KB ${WINDOWS_MAX_SIZE_KB})
$(set_define TCMALLOC_TARGET_TRANSFER_KB ${WINDOWS_TARGET_TRANSFER_KB})
$(set_define TCMALLOC_USE_UNCLAMPED_TRANSFER_SIZES 1)
" \
< $DEST_DIR/dist/src/windows/config.h \
> $WINDOWS_CONFIG/src/config.h
