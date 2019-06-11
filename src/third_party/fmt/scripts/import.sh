#!/bin/bash
# This script downloads and imports libfmt.

set -vxeuo pipefail

FMT_GIT_URL="https://github.com/mongodb-forks/fmt.git"
FMT_GIT_REV=87fbc6f7566e4d3266bd3a2cd69f6c90e1aefa5d # 2019-06-11
FMT_GIT_DIR=$(mktemp -d /tmp/import-fmt.XXXXXX)
trap "rm -rf $FMT_GIT_DIR" EXIT

DIST=$(git rev-parse --show-toplevel)/src/third_party/fmt/dist
git clone "$FMT_GIT_URL" $FMT_GIT_DIR
git -C $FMT_GIT_DIR checkout $FMT_GIT_REV

# Exclude the file 'include/format', which provides experimental
# 'std::' definitions.
SUBDIR_WHITELIST=(
    src
    include/fmt
    LICENSE.rst
)
for subdir in ${SUBDIR_WHITELIST[@]}; do
    [[ -d $FMT_GIT_DIR/$subdir ]] && mkdir -p $DIST/$subdir
    cp -Trp $FMT_GIT_DIR/$subdir $DIST/$subdir
done
