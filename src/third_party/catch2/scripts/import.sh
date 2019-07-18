#!/bin/bash
# This script downloads and imports Catch2

set -vxeuo pipefail

LIB_GIT_URL="git@github.com:catchorg/Catch2.git"
LIB_GIT_REV="v2.9.1" # 2019-07-17
LIB_GIT_DIR=$(mktemp -d /tmp/import-catch2.XXXXXX)
trap "rm -rf $LIB_GIT_DIR" EXIT

DIST=$(git rev-parse --show-toplevel)/src/third_party/catch2/dist
git clone "$LIB_GIT_URL" $LIB_GIT_DIR
git -C $LIB_GIT_DIR checkout $LIB_GIT_REV

cp -rp $LIB_GIT_DIR $DIST
