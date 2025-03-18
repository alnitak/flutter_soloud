#!/bin/bash

# This script builds the Ogg and Opus libraries for macOS
# Make sure to install the needed tools: `brew install autoconf automake libtool wget`

# Exit on any error
set -e

# Clone repositories if they don't exist
if [ ! -d "ogg" ]; then
    git clone https://github.com/xiph/ogg
    # reset to a known good commit
    cd ogg
    git reset --hard db5c7a4
    cd ..
fi

if [ ! -d "opus" ]; then
    git clone https://github.com/xiph/opus
    cd opus
    git reset --hard c79a9bd
    cd ..
fi

# Directories for source code and build output
LIBS=("ogg" "opus")
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/macos/build"
OUTPUT_DIR="$BASE_DIR/macos/libs"
INCLUDE_DIR="$BASE_DIR/macos/include"
ARCHS=("arm64" "x86_64")

# macOS SDK path
MACOS_SDK="$(xcrun --sdk macosx --show-sdk-path)"

# Create build and output directories
mkdir -p $BUILD_DIR
mkdir -p $OUTPUT_DIR
mkdir -p $INCLUDE_DIR

# Function to build a library for a specific architecture
build_lib() {
    local lib_name=$1
    local arch=$2
    local output_dir="$BUILD_DIR/$lib_name/$arch"

    echo "Building $lib_name for macOS ($arch)..."

    cd "$lib_name"
    ./autogen.sh

    # Configure and build with size optimization for opus
    if [ "$lib_name" == "opus" ]; then
        CFLAGS="-arch $arch -isysroot $MACOS_SDK -mmacosx-version-min=10.13 -Os -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables" \
        ./configure --host=$arch-apple-darwin --prefix="$output_dir" --disable-shared --disable-extra-programs
    else
        CFLAGS="-arch $arch -isysroot $MACOS_SDK -mmacosx-version-min=10.13 -O2" \
        ./configure --host=$arch-apple-darwin --prefix="$output_dir" --disable-shared
    fi
    make clean
    make -j$(sysctl -n hw.ncpu)
    make install
    cd ..
}

# Build libraries for each architecture
for lib in "${LIBS[@]}"; do
    for arch in "${ARCHS[@]}"; do
        build_lib $lib $arch
    done
done

# Create universal binaries and copy include files
for lib in "${LIBS[@]}"; do
    echo "Creating universal binary for $lib..."

    # Create universal binary
    lipo -create \
        "$BUILD_DIR/$lib/arm64/lib/lib${lib}.a" \
        "$BUILD_DIR/$lib/x86_64/lib/lib${lib}.a" \
        -output "$OUTPUT_DIR/lib${lib}.a"

    # Copy include files (from either arch, they're the same)
    cp -R "$BUILD_DIR/$lib/arm64/include/"* "$INCLUDE_DIR/"
done

# After creating universal binaries, strip them
for lib in "${LIBS[@]}"; do
    echo "Stripping symbols from $lib..."
    strip -x "$OUTPUT_DIR/lib${lib}.a"
done

echo
echo "Libraries created in $OUTPUT_DIR:"
ls -l $OUTPUT_DIR
echo
echo "Include files copied to $INCLUDE_DIR:"
ls -l $INCLUDE_DIR
