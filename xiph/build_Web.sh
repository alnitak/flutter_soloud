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
# LIBS=("ogg" "opus")
LIBS=("opus")
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/build"
OUTPUT_DIR="$BASE_DIR/../web"

# rm -rf $BUILD_DIR
# rm -rf $OUTPUT_DIR

# Create build and output directories
mkdir -p $BUILD_DIR
mkdir -p $OUTPUT_DIR

# Function to build a library for a specific architecture
build_lib() {
    local lib_name=$1
    local output_dir="$BUILD_DIR/$lib_name"

    echo "Building $lib_name..."

    cd "$lib_name"

    # Common build steps for both libraries
    ./autogen.sh
    
    if [ "$lib_name" = "ogg" ]; then
        emconfigure ./configure CFLAGS="-O3 -fPIC -g3 -gdwarf-4"
        emmake make

        emcc -O3 -g3 \
            src/.libs/libogg.a \
            -s WASM=1 \
            -s INITIAL_MEMORY=67108864 \
            -s MAXIMUM_MEMORY=2147483648 \
            -s STACK_SIZE=5242880 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s SAFE_HEAP=1 \
            -s ASSERTIONS=2 \
            -s STACK_OVERFLOW_CHECK=2 \
            -s DEMANGLE_SUPPORT=1 \
            -s EXPORT_ALL=1 \
            -s ERROR_ON_UNDEFINED_SYMBOLS=1 \
            -s EXPORTED_FUNCTIONS="['_malloc', '_free', '_memcpy', '_memset', '_ogg_sync_init', '_ogg_sync_clear', '_ogg_sync_buffer', '_ogg_sync_wrote', '_ogg_sync_pageout', '_ogg_stream_init', '_ogg_stream_pagein', '_ogg_stream_packetout', '_ogg_stream_clear', '_ogg_page_serialno']" \
            -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap', 'setValue', 'getValue', 'addFunction', 'removeFunction']" \
            -s ENVIRONMENT='web' \
            -s MALLOC='emmalloc' \
            -s MODULARIZE=1 \
            -s EXPORT_NAME="'Module_$lib_name'" \
            -s RUNTIME_DEBUG=1 \
            -o "$OUTPUT_DIR/$lib_name.js"

    elif [ "$lib_name" = "opus" ]; then

        emconfigure ./configure \
            --disable-extra-programs \
            --disable-doc \
            --disable-rtcd \
            --disable-intrinsics
            
        emmake make

        emcc -O3 \
            .libs/libopus.a \
            -s WASM=1 \
            -s INITIAL_MEMORY=67108864 \
            -s MAXIMUM_MEMORY=2147483648 \
            -s STACK_SIZE=5242880 \
            -s EXPORTED_FUNCTIONS="['_malloc', '_free', '_opus_decoder_create', '_opus_decoder_destroy', '_opus_decode', '_opus_decode_float', '_opus_strerror']" \
            -s EXPORTED_RUNTIME_METHODS="['ccall', 'cwrap', 'setValue', 'getValue']" \
            -s EXPORT_ALL=1 \
            -s NO_EXIT_RUNTIME=1 \
            -s MODULARIZE=1 \
            -s SAFE_HEAP=1 \
            -s ASSERTIONS=2 \
            -s ALLOW_MEMORY_GROWTH=1 \
            -s EXPORT_NAME="'Module_$lib_name'" \
            -o "$OUTPUT_DIR/$lib_name.js"
    fi

    cd ..
}

# Create output libraries
for lib in "${LIBS[@]}"; do
    echo "Creating libraries for $lib..."
    build_lib $lib
done

echo
echo
echo "Libraries created in $OUTPUT_DIR:"
ls -la $OUTPUT_DIR
