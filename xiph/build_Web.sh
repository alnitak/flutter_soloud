#!/bin/bash

# This script builds the Ogg and Opus libraries for WebAssembly
#
# emscripten must be installed and activated before running this script
# The script will git clone the libs, compile them and makes a fat library for iOS and MacOS

# Exit on any error
set -e

# Clone repositories if they don't exist
if [ ! -d "ogg" ]; then
    git clone https://github.com/xiph/ogg
fi

if [ ! -d "opus" ]; then
    git clone https://github.com/xiph/opus
fi

# Directories for source code and build output
LIBS=("ogg" "opus")
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/web/build"
OUTPUT_DIR="$BASE_DIR/web/libs"

# Create build and output directories
mkdir -p $BUILD_DIR
mkdir -p $OUTPUT_DIR

# Function to build a library for a specific architecture
build_lib() {
    local lib_name=$1
    local output_dir="$BUILD_DIR/$lib_name"
    local target_dir="$OUTPUT_DIR/$lib_name"

    echo "Building $lib_name..."

    mkdir -p "$target_dir"
    cd "$lib_name"
    ./autogen.sh

    # Configure and build with Emscripten
    emcmake cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$output_dir"

    cd build
    emmake make -j$(sysctl -n hw.ncpu)

    # Build with specific Emscripten flags
    emcc -O3 \
        -s WASM=1 \
        -s EXPORTED_FUNCTIONS="['_malloc', '_free']" \
        -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap']" \
        ./*.a \
        -o "$target_dir/$lib_name.js"

    cd ../..
}

# Create output libraries
for lib in "${LIBS[@]}"; do
    echo "Creating libraries for $lib..."
    build_lib $lib
done

echo
echo
echo "Libraries created in $OUTPUT_DIR:"
ls -R $OUTPUT_DIR
