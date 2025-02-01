#!/bin/bash

set -euo pipefail

# Get number of CPU cores for parallel compilation
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

FORCE_REBUILD_LIBS=0

# Directories
XIPH_DIR="../xiph"
OPUS_DIR="$XIPH_DIR/opus"
OGG_DIR="$XIPH_DIR/ogg"

# Clean and create build directory
rm -f libflutter_soloud_plugin.*

# Clone Opus and Ogg if not exists
if [ ! -d "$OPUS_DIR" ]; then
    git clone https://github.com/xiph/opus "$OPUS_DIR"
fi

if [ ! -d "$OGG_DIR" ]; then
    git clone https://github.com/xiph/ogg "$OGG_DIR"
fi

# Build Ogg if not built or force rebuild is set
if [ ! -f "$OGG_DIR/src/.libs/libogg.a" ] || [ $FORCE_REBUILD_LIBS -eq 1 ]; then
    cd "$OGG_DIR"
    ./autogen.sh
    emconfigure ./configure CFLAGS="-O3 -fPIC"
    emmake make -j$CORES
    cd -
fi

# Build Opus if not built or force rebuild is set
if [ ! -f "$OPUS_DIR/.libs/libopus.a" ] || [ $FORCE_REBUILD_LIBS -eq 1 ]; then
    cd "$OPUS_DIR"
    ./autogen.sh
    emconfigure ./configure \
        --disable-extra-programs \
        --disable-doc \
        --disable-rtcd \
        --disable-intrinsics \
        CFLAGS="-O3 -fPIC"
    emmake make -j$CORES
    cd -
fi

# Check if we need to recompile the final output
SOURCES=(
    ../src/soloud/src/core/*.c*
    ../src/soloud/src/filter/*.c*
    ../src/soloud/src/backend/miniaudio/*.c*
    ../src/soloud/src/audiosource/ay/*.c*
    ../src/soloud/src/audiosource/speech/*.c*
    ../src/soloud/src/audiosource/wav/*.c*
    ../src/common.cpp
    ../src/bindings.cpp
    ../src/player.cpp
    ../src/analyzer.cpp
    ../src/synth/*.cpp
    ../src/filters/*.cpp
    ../src/waveform/*.cpp
    ../src/audiobuffer/*.cpp
)

# Now compile everything together
em++ -O3 \
    -I ../src/soloud/include \
    -I ../src/soloud/src \
    -I ../src \
    -I ../src/filters \
    -I ../src/synth \
    -I "$OPUS_DIR/include" \
    -I "$OGG_DIR/include" \
    ${SOURCES[@]} \
    "$OPUS_DIR/.libs/libopus.a" \
    "$OGG_DIR/src/.libs/libogg.a" \
    -D WITH_MINIAUDIO \
    -msimd128 -msse3 \
    -std=c++17 \
    -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','setValue','getValue']" \
    -s "EXPORTED_FUNCTIONS=['_free', '_malloc', '_memcpy', '_memset']" \
    -s NO_EXIT_RUNTIME=1 \
    -s SAFE_HEAP=1 \
    -s STACK_SIZE=4194304 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=67108864 \
    -s MAXIMUM_MEMORY=2147483648 \
    -s EXPORT_ALL=1 -s NO_EXIT_RUNTIME=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="'Module_soloud'" \
    -o ../web/libflutter_soloud_plugin.js

echo
echo "Build completed successfully"
