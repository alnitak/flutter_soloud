#!/bin/bash


# This script builds the Ogg and Opus libraries for Android.
# NOTE: tested only running on Linux.
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
OUTPUT_INCLUDE_DIR="$BASE_DIR/android/include"
ARCHS=("arm64-v8a" "armeabi-v7a" "x86" "x86_64")

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"
mkdir -p "$OUTPUT_INCLUDE_DIR"

# Clone repositories if needed
if [ ! -d "ogg" ]; then
    git clone https://github.com/xiph/ogg
    # reset to a known good commit
    cd ogg
    git reset --hard db5c7a4
    cd ..
fi

if [ ! -d "opus" ]; then
    git clone https://github.com/xiph/opus
    cd opus
    git reset --hard c79a9bd
    cd ..
fi

# Function to build library for specific architecture
build_lib() {
    local lib=$1
    local arch=$2
    local build_path="$BUILD_DIR/${lib}_${arch}"
    local install_path="$OUTPUT_DIR/$arch"
    local temp_install_path="$build_path/install"
    
    echo "Building $lib for $arch..."
    
    # Create build directory
    mkdir -p "$build_path"
    mkdir -p "$install_path"
    mkdir -p "$temp_install_path"
    
    # Navigate to build directory
    cd "$build_path"
    
    # Configure and build
    cmake "$BASE_DIR/$lib" \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$arch" \
        -DANDROID_PLATFORM=android-21 \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$temp_install_path" \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_C_FLAGS="-Os -flto -ffunction-sections -fdata-sections" \
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -flto" \
        -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--gc-sections -flto" \
        -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG"

    cmake --build . --config Release --target install
    
    # Copy only the library files to the final location
    cp "$temp_install_path/lib/lib"*.so "$install_path/"
    
    # Strip debug symbols after copying
    $ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip "$install_path/lib"*.so
    
    # Copy headers (only needs to be done once per library)
    if [ "$arch" = "arm64-v8a" ]; then
        echo "Copying headers for $lib..."
        if [ "$lib" = "ogg" ]; then
            cp -r "$temp_install_path/include/ogg" "$OUTPUT_INCLUDE_DIR/"
        elif [ "$lib" = "opus" ]; then
            cp -r "$temp_install_path/include/opus" "$OUTPUT_INCLUDE_DIR/"
        fi
    fi
    
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
echo "Libraries created in $OUTPUT_DIR:"
ls -l $OUTPUT_DIR
echo
echo "Include files copied to $OUTPUT_INCLUDE_DIR:"
ls -l $OUTPUT_INCLUDE_DIR
