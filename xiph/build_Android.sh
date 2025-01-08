#!/bin/bash


# This script builds the Ogg and Opus libraries for Android
# Required: Android NDK, CMake, Git

# Exit on any error
set -e

# Check for Android NDK
if [ -z "$ANDROID_NDK_HOME" ]; then
    echo "Please set ANDROID_NDK_HOME environment variable"
    exit 1
fi

# Directories setup
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/android/build"
OUTPUT_DIR="$BASE_DIR/android/libs"
ARCHS=("arm64-v8a" "armeabi-v7a" "x86" "x86_64")

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"

# Clone repositories if needed
if [ ! -d "ogg" ]; then
    git clone https://github.com/xiph/ogg
fi

if [ ! -d "opus" ]; then
    git clone https://github.com/xiph/opus
fi

# Function to build library for specific architecture
build_lib() {
    local lib=$1
    local arch=$2
    local build_path="$BUILD_DIR/${lib}_${arch}"
    local install_path="$OUTPUT_DIR/$arch"
    
    echo "Building $lib for $arch..."
    
    # Create build directory
    mkdir -p "$build_path"
    mkdir -p "$install_path"
    
    # Navigate to build directory
    cd "$build_path"
    
    # Configure and build
    cmake "$BASE_DIR/$lib" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$arch" \
        -DANDROID_PLATFORM=android-21 \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$install_path" \
        -DBUILD_SHARED_LIBS=ON
    
    cmake --build . --config Release --target install
    
    # Go back to base directory
    cd "$BASE_DIR"
}

# Build both libraries for all architectures
for arch in "${ARCHS[@]}"; do
    echo "=== Building for $arch ==="
    build_lib "ogg" "$arch"
    build_lib "opus" "$arch"
done

echo
echo
echo "Build complete! Libraries are in $OUTPUT_DIR"
