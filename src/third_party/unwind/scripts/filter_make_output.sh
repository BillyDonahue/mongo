#!/bin/bash

# We use the output of 'make' to inform our SConscript rules.
# Here we attempt to make that manual hacking process at least a little bit durable.
# Must be run after scripts/host_config.sh has set up the build dir.
DEST_DIR=$(git rev-parse --show-toplevel)/src/third_party/unwind

HOST_OS="$(uname -s|tr A-Z a-z)"
HOST_ARCH="$(uname -m)"
HOST_DIR="$DEST_DIR/platform/${HOST_OS}_${HOST_ARCH}"
SRC_DIR=${DEST_DIR}/dist

mkdir -p $HOST_DIR/build
pushd $HOST_DIR/build


GCC_BOILER='libtool: compile: /opt/mongodbtoolchain/v3/bin/gcc -DHAVE_CONFIG_H -I. -I$DEST_DIR/dist/src -I../include -I$DEST_DIR/dist/include -I$DEST_DIR/dist/include/tdep-x86_64 -I. -D_GNU_SOURCE -DNDEBUG -g -O2 -fexceptions -Wall -Wsign-compare'

make clean
make|
    sed \
        -e 's/  */ /g' \
        -e '/^\/bin\/bash \.\.\/libtool/d' \
        -e '/^make\[/d' \
        -e 's/^libtool: compile: /${COMPILE} /g' \
        -e "s#$DEST_DIR#\${DEST_DIR}#g" |
    tee make.log

