#!/bin/bash

set -euo pipefail

BOLD_WHITE_ON_GREEN="\e[1;37;42m"
RESET="\e[0m"

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
XIPH_DIR="$PWD/../xiph"
OPUS_DIR="$XIPH_DIR/opus"
OGG_DIR="$XIPH_DIR/ogg"
VORBIS_DIR="$XIPH_DIR/vorbis"
FLAC_DIR="$XIPH_DIR/flac"

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

    if [ ! -d "$VORBIS_DIR" ]; then
        git clone https://github.com/xiph/vorbis "$VORBIS_DIR"
        # reset to a known good commit
        cd "$VORBIS_DIR"
        git reset --hard 84c0236
        cd -
    fi

    if [ ! -d "$FLAC_DIR" ]; then
        git clone https://github.com/xiph/flac "$FLAC_DIR"
        # reset to a known good commit
        cd "$FLAC_DIR"
        git reset --hard 9547dbc
        cd -
    fi

    # Build Ogg if not built or force rebuild is set
    echo -e "${BOLD_WHITE_ON_GREEN}Building Ogg${RESET}"
    if [ ! -f "$OGG_DIR/src/.libs/libogg.a" ] || [ $FORCE_REBUILD_LIBS -eq 1 ]; then
        cd "$OGG_DIR"
        ./autogen.sh
        emconfigure ./configure CFLAGS="-O2 -fPIC"
        emmake make -j$CORES
        cd -
    fi

    # Build Opus if not built or force rebuild is set
    echo -e "${BOLD_WHITE_ON_GREEN}Building Opus${RESET}"
    if [ ! -f "$OPUS_DIR/.libs/libopus.a" ] || [ $FORCE_REBUILD_LIBS -eq 1 ]; then
        cd "$OPUS_DIR"
        ./autogen.sh
        emconfigure ./configure \
            --disable-extra-programs \
            --disable-doc \
            --disable-rtcd \
            --disable-intrinsics \
            CFLAGS="-O2 -fPIC"
        emmake make -j$CORES
        cd -
    fi

    # Build Vorbis if not built or force rebuild is set
    echo -e "${BOLD_WHITE_ON_GREEN}Building Vorbis${RESET}"
    if [ ! -f "$VORBIS_DIR/lib/.libs/libvorbis.a" ] || [ $FORCE_REBUILD_LIBS -eq 1 ]; then
        cd "$VORBIS_DIR"
        ./autogen.sh
        emconfigure ./configure \
            CFLAGS="-O2 -fPIC -L$OGG_DIR/src/.libs -I$OGG_DIR/include" \
            --with-ogg="$OGG_DIR"
        emmake make -j$CORES
        cd -
    fi

    # Build Flac if not built or force rebuild is set
    echo -e "${BOLD_WHITE_ON_GREEN}Building Flac${RESET}"
    if [ ! -f "$FLAC_DIR/src/libFLAC/.libs/libFLAC-static.a" ] || [ $FORCE_REBUILD_LIBS -eq 1 ]; then
        cd "$FLAC_DIR"
        ./autogen.sh
        emconfigure ./configure \
            CFLAGS="-O2 -fPIC -L$OGG_DIR/src/.libs -I$OGG_DIR/include" \
            --with-ogg="$OGG_DIR" \
            --disable-cpplibs \
            --disable-doxygen-docs \
            --disable-xmms-plugin \
            --disable-cpplibs \
            --disable-programs \
            --disable-examples
        emmake make -j$CORES
        cd -
    fi

    echo -e "${BOLD_WHITE_ON_GREEN}Building libraries completed!${RESET}"
fi

    echo
    echo -e "${BOLD_WHITE_ON_GREEN}Start building flutter_soloud!${RESET}"

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
        -I "$VORBIS_DIR/include"
        -I "$FLAC_DIR/include"
    )
fi

LIBS=()
if [ "${SKIP_OPUS_OGG}" != "1" ]; then
    LIBS+=(
        "$OPUS_DIR/.libs/libopus.a"
        "$OGG_DIR/src/.libs/libogg.a"
        "$VORBIS_DIR/lib/.libs/libvorbis.a"
        "$VORBIS_DIR/lib/.libs/libvorbisfile.a"
        "$FLAC_DIR/src/libFLAC/.libs/libFLAC-static.a"
    )
fi

# Define compiler flags based on NO_OPUS_OGG_LIBS
COMPILER_DEFINES="-D WITH_MINIAUDIO"
if [ "${SKIP_OPUS_OGG}" = "1" ]; then
    COMPILER_DEFINES="$COMPILER_DEFINES -D NO_OPUS_OGG_LIBS"
fi

# Now compile everything together
    # -s ASSERTIONS=1 \
    # -g -fdebug-compilation-dir=./debug \
    # -s NO_DISABLE_EXCEPTION_CATCHING=1 \
em++ -O2 \
    ${INCLUDE_DIRS[@]} \
    ${SOURCES[@]} \
    ${LIBS[@]} \
    ${COMPILER_DEFINES} \
    -msimd128 -msse3 \
    -std=c++17 \
    -s "EXPORTED_RUNTIME_METHODS=['ccall','cwrap','setValue','getValue','UTF8ToString','HEAPF32','HEAPU8']" \
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
echo -e "${BOLD_WHITE_ON_GREEN}Build completed successfully...${RESET}"
if [ "${SKIP_OPUS_OGG}" != "1" ]; then
    echo -e "${BOLD_WHITE_ON_GREEN}with Opus and Ogg${RESET}"
else
    echo -e "${BOLD_WHITE_ON_GREEN}without Opus and Ogg${RESET}"
fi
