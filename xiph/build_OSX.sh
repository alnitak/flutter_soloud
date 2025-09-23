#!/bin/bash

# This script builds the Ogg and Opus libraries for macOS
# Make sure to install the needed tools: `brew install autoconf automake libtool wget`

# Exit on any error
set -e

BOLD_WHITE_ON_GREEN=$'\e[1;37;42m'
RESET=$'\e[0m'

# Clone repositories if they don't exist
if [ ! -d "ogg" ]; then
    git clone https://github.com/xiph/ogg
    # reset to a known good commit
    cd ogg
    git reset --hard db5c7a4
    cd ..
fi

if [ ! -d "vorbis" ]; then
    git clone https://github.com/xiph/vorbis
    cd vorbis
    git reset --hard 84c0236
    # Remove -force_cpusubtype_ALL flag from configure.ac because it was
    # historically used in build scripts for compatibility with very
    # old Mac hardware (PowerPC) and prevent the build.
    sed -i '' 's/-force_cpusubtype_ALL//g' configure.ac
    cd ..
fi

if [ ! -d "opus" ]; then
    git clone https://github.com/xiph/opus
    cd opus
    git reset --hard c79a9bd
    cd ..
fi

if [ ! -d "flac" ]; then
    git clone https://github.com/xiph/flac
    cd flac
    git reset --hard 9547dbc
    cd ..
fi

# Directories for source code and build output
LIBS=("ogg" "opus" "vorbis" "flac")
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/macos/build"
OUTPUT_DIR="$BASE_DIR/../macos/libs"
INCLUDE_DIR="$BASE_DIR/../macos/include"
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
    local host_triplet

    if [ "$arch" == "arm64" ]; then
        host_triplet="aarch64-apple-darwin"
    else
        host_triplet="x86_64-apple-darwin"
    fi

    echo "${BOLD_WHITE_ON_GREEN}Building $lib_name for macOS ($arch)...${RESET}"

    cd "$lib_name"
    ./autogen.sh

    # Configure and build with size optimization for opus
    if [ "$lib_name" == "opus" ]; then
        CFLAGS="-arch $arch -isysroot $MACOS_SDK -mmacosx-version-min=10.13 -Os -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables" \
        ./configure --host=$host_triplet --prefix="$output_dir" --disable-shared --disable-extra-programs
    fi
    if [ "$lib_name" == "ogg" ]; then
        CFLAGS="-arch $arch -isysroot $MACOS_SDK -mmacosx-version-min=10.13 -O2" \
        ./configure --host=$host_triplet --prefix="$output_dir" --disable-shared
    fi
    if [ "$lib_name" == "vorbis" ]; then
        PKG_CONFIG_PATH="$BUILD_DIR/ogg/$arch/lib/pkgconfig" \
        # vorbis is the last build so it can depend on already compiled ogg lib
        LDFLAGS="-L$BUILD_DIR/ogg/$arch/lib" \
        CPPFLAGS="-I$BUILD_DIR/ogg/$arch/include" \
        CFLAGS="-arch $arch -isysroot $MACOS_SDK -mmacosx-version-min=10.13 -O2" \
        ./configure --host=$host_triplet --prefix="$output_dir" --disable-shared --with-ogg="$BUILD_DIR/ogg/$arch"
    fi
    if [ "$lib_name" == "flac" ]; then
        PKG_CONFIG_PATH="$BUILD_DIR/ogg/$arch/lib/pkgconfig" \
        LDFLAGS="-L$BUILD_DIR/ogg/$arch/lib" \
        CPPFLAGS="-I$BUILD_DIR/ogg/$arch/include" \
        CFLAGS="-arch $arch -isysroot $MACOS_SDK -mmacosx-version-min=10.13 -O2" \
        ./configure --host=$host_triplet --prefix="$output_dir" --disable-shared --with-ogg="$BUILD_DIR/ogg/$arch" \
            --disable-cpplibs \
            --disable-doxygen-docs \
            --disable-xmms-plugin \
            --disable-programs \
            --disable-examples
    fi

    make clean
    make -j$(sysctl -n hw.ncpu)
    make install
    cd "$BASE_DIR"
}

# Build libraries for each architecture
for lib in "${LIBS[@]}"; do
    for arch in "${ARCHS[@]}"; do
        build_lib $lib $arch
    done
done

echo "${BOLD_WHITE_ON_GREEN}=== Removing not used vorbisenc* ===${RESET}"
for arch in "${ARCHS[@]}"; do
    rm "$BUILD_DIR/vorbis/$arch/lib/libvorbisenc."*
    rm "$BUILD_DIR/vorbis/$arch/include/vorbis/vorbisenc.h"
done

# Create universal binaries and copy include files
for lib in "${LIBS[@]}"; do
    echo "${BOLD_WHITE_ON_GREEN}Creating universal binary for $lib...=${RESET}"

    if [ "$lib" == "flac" ]; then
        lipo -create \
            "$BUILD_DIR/$lib/arm64/lib/libFLAC.a" \
            "$BUILD_DIR/$lib/x86_64/lib/libFLAC.a" \
            -output "$OUTPUT_DIR/libFLAC.a"
    else
        lipo -create \
            "$BUILD_DIR/$lib/arm64/lib/lib${lib}.a" \
            "$BUILD_DIR/$lib/x86_64/lib/lib${lib}.a" \
            -output "$OUTPUT_DIR/lib${lib}.a"
    fi

    if [ "$lib" == "vorbis" ]; then
        echo "${BOLD_WHITE_ON_GREEN}Creating universal binary for vorbisfile...=${RESET}"
        lipo -create \
            "$BUILD_DIR/$lib/arm64/lib/libvorbisfile.a" \
            "$BUILD_DIR/$lib/x86_64/lib/libvorbisfile.a" \
            -output "$OUTPUT_DIR/libvorbisfile.a"
    fi

    # Copy include files (from either arch, they're the same)
    cp -R "$BUILD_DIR/$lib/arm64/include/"* "$INCLUDE_DIR/"
done

cp -R "$BASE_DIR/flac/include/share" "$INCLUDE_DIR/"

# After creating universal binaries, strip them
for lib in "${LIBS[@]}"; do
    echo "${BOLD_WHITE_ON_GREEN}Stripping symbols from $lib...=${RESET}"
    if [ "$lib" == "flac" ]; then
        strip -x "$OUTPUT_DIR/libFLAC.a"
    else
        strip -x "$OUTPUT_DIR/lib${lib}.a"
    fi
    if [ "$lib" == "vorbis" ]; then
        echo "${BOLD_WHITE_ON_GREEN}Stripping symbols from vorbisfile...=${RESET}"
        strip -x "$OUTPUT_DIR/libvorbisfile.a"
    fi
done

echo
echo "${BOLD_WHITE_ON_GREEN}Libraries created in $OUTPUT_DIR:=${RESET}"
ls -l $OUTPUT_DIR
echo
echo "${BOLD_WHITE_ON_GREEN}Include files copied to $INCLUDE_DIR:=${RESET}"
ls -l $INCLUDE_DIR
