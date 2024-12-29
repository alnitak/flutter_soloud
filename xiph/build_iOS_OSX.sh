#!/bin/bash

# This script builds the Ogg and Opus libraries for iOS and macOS
#
# 1) Make your dir where opus and ogg libraries will be downloaded
# 2) Copy this script and run it
# The script will git clone the libs, compile them and makes a fat library for iOS and MacOS

# Exit on any error
set -e

git clone https://github.com/xiph/ogg
git clone https://github.com/xiph/opus

# Directories for source code and build output
LIBS=("ogg" "opus")
BASE_DIR="/Users/deimos/xiph"
BUILD_DIR="$BASE_DIR/build"
OUTPUT_DIR="$BASE_DIR/fat_libs"
ARCHS_MACOS=("x86_64" "arm64")
ARCHS_IOS=("arm64" "x86_64")  # iOS arm64 (device) and x86_64 (Simulator)

# iOS-specific flags
IOS_SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
SIMULATOR_SDK="$(xcrun --sdk iphonesimulator --show-sdk-path)"

# Create build and output directories
mkdir -p $BUILD_DIR
mkdir -p $OUTPUT_DIR

# Function to build a library for a specific architecture
build_lib() {
    local lib_name=$1
    local arch=$2
    local platform=$3
    local sdk=$4
    local output_dir="$BUILD_DIR/$lib_name/$platform/$arch"

    echo "Building $lib_name for $platform ($arch)..."

    cd "$lib_name"
    ./autogen.sh  # Generate configure script if necessary

    # Configure and build
    CFLAGS="-arch $arch -isysroot $sdk" \
    ./configure --host=$arch-apple-darwin --prefix="$output_dir" --disable-shared
    make clean
    make -j$(sysctl -n hw.ncpu)
    make install
    cd ..
}

# Build macOS libraries
for lib in "${LIBS[@]}"; do
    for arch in "${ARCHS_MACOS[@]}"; do
        build_lib $lib $arch "macOS" "$(xcrun --sdk macosx --show-sdk-path)"
    done
done

# Build iOS libraries
for lib in "${LIBS[@]}"; do
    for arch in "${ARCHS_IOS[@]}"; do
        if [ "$arch" == "x86_64" ]; then
            sdk=$SIMULATOR_SDK  # iOS Simulator
            platform="iOS_Simulator"
        else
            sdk=$IOS_SDK  # iOS Device
            platform="iOS"
        fi
        build_lib $lib $arch "$platform" "$sdk"
    done
done

# Create fat libraries using lipo
for lib in "${LIBS[@]}"; do
    echo "Creating fat libraries for $lib..."

    # macOS fat library
    INPUT_LIBS_MACOS=()
    for arch in "${ARCHS_MACOS[@]}"; do
        INPUT_LIBS_MACOS+=("$BUILD_DIR/$lib/macOS/$arch/lib/lib${lib}.a")
    done
    lipo -create -output "$OUTPUT_DIR/lib${lib}_macOS.a" "${INPUT_LIBS_MACOS[@]}"

    # iOS fat library
    INPUT_LIBS_IOS=()
    for arch in "${ARCHS_IOS[@]}"; do
        platform="iOS_Simulator"
        if [ "$arch" == "arm64" ]; then
            platform="iOS"
        fi
        INPUT_LIBS_IOS+=("$BUILD_DIR/$lib/$platform/$arch/lib/lib${lib}.a")
    done
    lipo -create -output "$OUTPUT_DIR/lib${lib}_iOS.a" "${INPUT_LIBS_IOS[@]}"
done

echo "Fat libraries created in $OUTPUT_DIR:"
ls -l $OUTPUT_DIR
