#!/bin/bash

# This script builds the Ogg and Opus libraries for iOS
#
# Make sure to install the needed tools: `brew install autoconf automake libtool wget`
# The script will git clone the libs, compile them and makes a fat library for iOS

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
    # reset to a known good commit
    cd opus
    git reset --hard c79a9bd
    cd ..
fi

# Directories for source code and build output
LIBS=("ogg" "opus")
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/iOS/build"
OUTPUT_DIR="$BASE_DIR/iOS/libs"
INCLUDE_DIR="$BASE_DIR/iOS/include"
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

    echo "Building $lib_name for $platform ($arch)..."

    cd "$lib_name"
    ./autogen.sh  # Generate configure script if necessary

    # Configure and build
    CFLAGS="-arch $arch -isysroot $sdk -O2" \
    ./configure --host=$arch-apple-darwin --prefix="$output_dir" --disable-shared
    make clean
    make -j$(sysctl -n hw.ncpu)
    make install
    cd ..
}

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

# Create output libraries
for lib in "${LIBS[@]}"; do
    echo "Creating libraries for $lib..."

    # iOS device library (arm64)
    cp "$BUILD_DIR/$lib/iOS/arm64/lib/lib${lib}.a" "$OUTPUT_DIR/lib${lib}_iOS-device.a"

    # iOS simulator library (x86_64)
    cp "$BUILD_DIR/$lib/iOS_Simulator/x86_64/lib/lib${lib}.a" "$OUTPUT_DIR/lib${lib}_iOS-simulator.a"

    # Copy include files (from either arch, they're the same)
    cp -R "$BUILD_DIR/$lib/iOS/arm64/include/"* "$INCLUDE_DIR/"

    # Strip symbols from both device and simulator libraries
    echo "Stripping symbols from $lib libraries..."
    strip -x "$OUTPUT_DIR/lib${lib}_iOS-device.a"
    strip -x "$OUTPUT_DIR/lib${lib}_iOS-simulator.a"
done

echo
echo "Libraries created in $OUTPUT_DIR:"
ls -l $OUTPUT_DIR
echo
echo "Include files copied to $INCLUDE_DIR:"
ls -l $INCLUDE_DIR
