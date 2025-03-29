#!/bin/bash

set -euo pipefail

# Comment this line or set it to 0 to build with opus and ogg.
# If set to 1, the plugin will be built without opus and ogg.
NO_OPUS_OGG_LIBS="0"

# Get number of CPU cores for parallel compilation
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

# Check if we should force rebuild ogg and opus and not only the whole plugin.
# Since it takes a while to build the libraries, we don't want to do it every time.
FORCE_REBUILD_LIBS=0

# Check if we should skip Opus/Ogg
if [ -n "${NO_OPUS_OGG_LIBS+x}" ] && [ "$NO_OPUS_OGG_LIBS" = "1" ]; then
    SKIP_OPUS_OGG="1"
else
    SKIP_OPUS_OGG="0"
fi

# Directories
XIPH_DIR="../xiph"
OPUS_DIR="$XIPH_DIR/opus"
OGG_DIR="$XIPH_DIR/ogg"

# Clean and create build directory
rm -f libflutter_soloud_plugin.*

# Handle Opus and Ogg compilation only if not skipped
if [ "${SKIP_OPUS_OGG}" != "1" ]; then
    # Clone Opus and Ogg if not exists
    if [ ! -d "$OPUS_DIR" ]; then
        git clone https://github.com/xiph/opus "$OPUS_DIR"
        # reset to a known good commit
        cd "$OPUS_DIR"
        git reset --hard c79a9bd
        cd -
    fi

    if [ ! -d "$OGG_DIR" ]; then
        git clone https://github.com/xiph/ogg "$OGG_DIR"
        # reset to a known good commit
        cd "$OGG_DIR"
        git reset --hard db5c7a4
        cd -
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

# Prepare include directories and libraries based on configuration
INCLUDE_DIRS=(
    -I ../src/soloud/include
    -I ../src/soloud/src
    -I ../src
    -I ../src/filters
    -I ../src/synth
)

if [ "${SKIP_OPUS_OGG}" != "1" ]; then
    INCLUDE_DIRS+=(
        -I "$OPUS_DIR/include"
        -I "$OGG_DIR/include"
    )
fi

LIBS=()
if [ "${SKIP_OPUS_OGG}" != "1" ]; then
    LIBS+=(
        "$OPUS_DIR/.libs/libopus.a"
        "$OGG_DIR/src/.libs/libogg.a"
    )
fi

# Define compiler flags based on NO_OPUS_OGG_LIBS
COMPILER_DEFINES="-D WITH_MINIAUDIO"
if [ "${SKIP_OPUS_OGG}" = "1" ]; then
    COMPILER_DEFINES="$COMPILER_DEFINES -D NO_OPUS_OGG_LIBS"
fi

# Now compile everything together
em++ -O3 \
    ${INCLUDE_DIRS[@]} \
    ${SOURCES[@]} \
    ${LIBS[@]} \
    ${COMPILER_DEFINES} \
    -msimd128 -msse3 \
    -std=c++17 \
    -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','setValue','getValue', 'UTF8ToString']" \
    -s "EXPORTED_FUNCTIONS=['_free', '_malloc', '_memcpy', '_memset']" \
    -s NO_EXIT_RUNTIME=1 \
    -s SAFE_HEAP=1 \
    -s STACK_SIZE=4194304 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s INITIAL_MEMORY=67108864 \
    -s MAXIMUM_MEMORY=2147483648 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="'Module_soloud'" \
    -o ../web/libflutter_soloud_plugin.js

echo
echo "Build completed successfully..."
if [ "${SKIP_OPUS_OGG}" != "1" ]; then
    echo "with Opus and Ogg"
else
    echo "without Opus and Ogg"
fi