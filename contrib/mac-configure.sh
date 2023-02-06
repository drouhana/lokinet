#!/bin/bash

if ! [ -f LICENSE ] || ! [ -d llarp ]; then
    echo "You need to run this as ./contrib/mac.sh from the top-level lokinet project directory" >&2
    exit 1
fi

if [ "$#" -lt 1 ]; then
    echo "Usage: $0 BUILDDIR {PACKAGE_NAME|\"\"} [...extra cmake args...]" >&2
    exit 1
fi

BUILDDIR=$1; shift

set -e
set -x

mkdir -p "$BUILDDIR"
cd "$BUILDDIR"

if [[ "$(brew info llvm 2>&1 | grep -c "Poured from bottle on")" == 1 ]]; then
    # we are using a homebrew clang, need new flags
    LDFLAGS+="-L/opt/homebrew/opt/llvm/lib"
    CXXFLAGS+="-I/opt/homebrew/opt/llvm/include"
fi

cmake \
    -G Ninja \
    -DBUILD_STATIC_DEPS=ON \
    -DWITH_TESTS=OFF \
    -DWITH_BOOTSTRAP=OFF \
    -DNATIVE_BUILD=OFF \
    -DWITH_LTO=ON \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=$(which clang) \
    -DCMAKE_CXX_COMPILER=$(which clang++) \
    -DMACOS_SYSTEM_EXTENSION=ON \
    -DCODESIGN=ON \
    -DBUILD_PACKAGE=ON \
    -DMACOS_NOTARIZE_USER="jagerman@jagerman.com" \
    -DMACOS_NOTARIZE_PASS="pass" \
    -DMACOS_NOTARIZE_ASC="SUQ8J2PCT7" \
    -DCODESIGN_ID="D2FF63B3074D4E84C13CE4D6B7879F7BFA00EB5E" \
    "$@" \
    ../..

echo "cmake build configured in $BUILDDIR"