#!/bin/bash

# This script builds the Ogg and Opus libraries for iOS
#
# Make sure to install the needed tools: `brew install autoconf automake libtool wget`
# The script will git clone the libs, compile them and makes a fat library for iOS

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
    # reset to a known good commit
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
BUILD_DIR="$BASE_DIR/iOS/build"
OUTPUT_DIR="$BASE_DIR/../iOS/libs"
INCLUDE_DIR="$BASE_DIR/../iOS/include"
ARCHS_IOS=("arm64" "x86_64")  # iOS arm64 (device) and x86_64 (Simulator)

# iOS-specific flags
IOS_SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
SIMULATOR_SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"

# Create build and output directories
mkdir -p $BUILD_DIR
mkdir -p $OUTPUT_DIR
mkdir -p $INCLUDE_DIR

# Function to build a library for a specific architecture
build_lib() {
    local lib_name=$1
    local arch=$2
    local platform=$3
    local sdk=$4
    local output_dir="$BUILD_DIR/$lib_name/$platform/$arch"

    echo "${BOLD_WHITE_ON_GREEN}Building $lib_name for $platform ($arch)...${RESET}"

    cd "$lib_name"
    ./autogen.sh  # Generate configure script if necessary

    # Configure and build
    if [ "$lib_name" == "vorbis" ]; then
        PKG_CONFIG_PATH="$BUILD_DIR/ogg/$arch/lib/pkgconfig" \
        # vorbis is the last build so it can depend on already compiled ogg lib
        LDFLAGS="-L$BUILD_DIR/ogg/$arch/lib" \
        CPPFLAGS="-I$BUILD_DIR/ogg/$arch/include" \
        CFLAGS="-arch $arch -isysroot $sdk -O2" \
        ./configure --host=$arch-apple-darwin --prefix="$output_dir" --disable-shared --with-ogg="$BUILD_DIR/ogg/$arch"
    elif [ "$lib_name" == "flac" ]; then
        PKG_CONFIG_PATH="$BUILD_DIR/ogg/$platform/$arch/lib/pkgconfig" \
        LDFLAGS="-L$BUILD_DIR/ogg/$platform/$arch/lib" \
        CPPFLAGS="-I$BUILD_DIR/ogg/$platform/$arch/include" \
        CFLAGS="-arch $arch -isysroot $sdk -O2" \
        ./configure --host=$arch-apple-darwin --prefix="$output_dir" --disable-shared --with-ogg="$BUILD_DIR/ogg/$platform/$arch" \
            --disable-cpplibs \
            --disable-doxygen-docs \
            --disable-xmms-plugin \
            --disable-programs \
            --disable-examples
    else
        CFLAGS="-arch $arch -isysroot $sdk -O2" \
        ./configure --host=$arch-apple-darwin --prefix="$output_dir" --disable-shared
    fi
    
    make clean
    make -j$(sysctl -n hw.ncpu)
    make install
    cd ..
}

# Build iOS libraries
for lib in "${LIBS[@]}"; do
  # device: arm64 (iphoneos)
  build_lib $lib arm64 "iOS" "$IOS_SDK"

  # simulator: x86_64 (iphonesimulator)
  build_lib $lib x86_64 "iOS_Simulator" "$SIMULATOR_SDK"

  # simulator: arm64 (iphonesimulator)
  build_lib $lib arm64 "iOS_Simulator" "$SIMULATOR_SDK"
done

echo "${BOLD_WHITE_ON_GREEN}=== Removing not used libvorbisenc* ===${RESET}"
    echo "${BOLD_WHITE_ON_GREEN}Removing: $BUILD_DIR/vorbis/iOS/arm64/lib/libvorbisenc.*${RESET}"
    rm "$BUILD_DIR/vorbis/iOS/arm64/lib/libvorbisenc."*
    echo "${BOLD_WHITE_ON_GREEN}Removing: $BUILD_DIR/vorbis/iOS/arm64/include/vorbis/vorbisenc.h${RESET}"
    rm "$BUILD_DIR/vorbis/iOS/arm64/include/vorbis/vorbisenc.h"


    echo "${BOLD_WHITE_ON_GREEN}Removing: $BUILD_DIR/vorbis/iOS_Simulator/x86_64/lib/libvorbisenc..*${RESET}"
    rm "$BUILD_DIR/vorbis/iOS_Simulator/x86_64/lib/libvorbisenc."*
    echo "${BOLD_WHITE_ON_GREEN}Removing: $BUILD_DIR/vorbis/iOS_Simulator/x86_64/include/vorbis/vorbisenc.h${RESET}"
    rm "$BUILD_DIR/vorbis/iOS_Simulator/x86_64/include/vorbis/vorbisenc.h"
echo

# Create output libraries
for lib in "${LIBS[@]}"; do
    echo "${BOLD_WHITE_ON_GREEN}Creating libraries for $lib...${RESET}"

    # iOS device library (arm64)
    cp "$BUILD_DIR/$lib/iOS/arm64/lib/lib${lib}.a" "$OUTPUT_DIR/lib${lib}_iOS-device.a"

    # iOS simulator FAT (x86_64 + arm64)
    lipo -create \
        "$BUILD_DIR/$lib/iOS_Simulator/x86_64/lib/lib${lib}.a" \
        "$BUILD_DIR/$lib/iOS_Simulator/arm64/lib/lib${lib}.a" \
        -output "$OUTPUT_DIR/lib${lib}_iOS-simulator.a"

    if [ "$lib" == "vorbis" ]; then
        cp "$BUILD_DIR/$lib/iOS/arm64/lib/libvorbisfile.a" "$OUTPUT_DIR/libvorbisfile_iOS-device.a"
        lipo -create \
            "$BUILD_DIR/$lib/iOS_Simulator/x86_64/lib/libvorbisfile.a" \
            "$BUILD_DIR/$lib/iOS_Simulator/arm64/lib/libvorbisfile.a" \
            -output "$OUTPUT_DIR/libvorbisfile_iOS-simulator.a"
    fi

    # Copy include files (from either arch, they're the same)
    cp -R "$BUILD_DIR/$lib/iOS/arm64/include/"* "$INCLUDE_DIR/"

    # Strip symbols from both device and simulator libraries
    echo "${BOLD_WHITE_ON_GREEN}Stripping symbols from $lib libraries...${RESET}"
    strip -x "$OUTPUT_DIR/lib${lib}_iOS-device.a"
    strip -x "$OUTPUT_DIR/lib${lib}_iOS-simulator.a"
done

cp -R "$BASE_DIR/flac/include/share" "$INCLUDE_DIR/"

echo
echo "${BOLD_WHITE_ON_GREEN}Libraries created in $OUTPUT_DIR:${RESET}"
ls -l $OUTPUT_DIR
echo
echo "${BOLD_WHITE_ON_GREEN}Include files copied to $INCLUDE_DIR:${RESET}"
ls -l $INCLUDE_DIR
