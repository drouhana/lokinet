#!/bin/bash
#
# Build the shit on mac
#
# You will generally need to add: -DCODESIGN_APP=... to make this work, and (unless you are a
# lokinet team member) will need to pay Apple money for your own team ID and arse around with
# provisioning profiles.  See macos/README.txt.
#

if ! [ -f LICENSE ] || ! [ -d llarp ]; then
    echo "You need to run this as ./contrib/mac.sh from the top-level lokinet project directory" >&2
    exit 1
fi

if ! command -v xcodebuild; then
    echo "xcodebuild not found; are you on macOS with Xcode and Xcode command-line tools installed?" >&2
    exit 1
fi

set -e
set -x

for ARCH in arm64 x86_64; do
    BUILDDIR="build-mac/$ARCH"

    if [ "$(uname -m)" == "$ARCH" ]; then
        echo "Building for macOS [ $ARCH ] in $BUILDDIR" >&2
        ./contrib/mac-configure.sh "$BUILDDIR"
    else
        echo "Cross-compiling for macOS [ $ARCH ] in $BUILDDIR" >&2
        ./contrib/mac-configure.sh "$BUILDDIR" \
            -DCMAKE_SYSTEM_NAME=Darwin \
            -DARCH_TRIPLET="$ARCH-apple-darwin$(uname -r)" \
            -DCMAKE_OSX_ARCHITECTURE=$ARCH
    fi

    cd "$BUILDDIR"
    rm -rf Lokinet\ *
    ninja -j${JOBS:-1} dmg
    cd ../..

    echo -e "Build complete... app is here: \n"
    ls -lad $PWD/$BUILDDIR/Lokinet\ *
    echo ""
done
