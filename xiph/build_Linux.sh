#!/bin/bash

# This script builds the Ogg and Opus libraries for Linux.
# Required: CMake, Git

# Exit on any error
set -e

# Directories setup
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/linux/build"
OUTPUT_DIR="$BASE_DIR/linux/libs"
OUTPUT_INCLUDE_DIR="$BASE_DIR/linux/include"

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"
mkdir -p "$OUTPUT_INCLUDE_DIR"

# Clone repositories if needed
if [ ! -d "ogg" ]; then
    git clone https://github.com/xiph/ogg
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

# Function to build library
build_lib() {
    local lib=$1
    local build_path="$BUILD_DIR/${lib}"
    local temp_install_path="$build_path/install"
    
    echo "Building $lib..."
    
    # Create build directory
    mkdir -p "$build_path"
    mkdir -p "$temp_install_path"
    
    # Navigate to build directory
    cd "$build_path"
    
    # Configure and build
    cmake "$BASE_DIR/$lib" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$temp_install_path" \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_C_FLAGS="-Os -flto -ffunction-sections -fdata-sections" \
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -flto" \
        -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--gc-sections -flto" \
        -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG"

    cmake --build . --config Release --target install
    
    # Copy the library files to the final location, preserving symlinks
    cp -P "$temp_install_path/lib/lib"*.so* "$OUTPUT_DIR/"
    
    # Strip debug symbols after copying
    strip "$OUTPUT_DIR/lib"*.so*
    
    # Copy headers
    if [ "$lib" = "ogg" ]; then
        cp -r "$temp_install_path/include/ogg" "$OUTPUT_INCLUDE_DIR/"
    elif [ "$lib" = "opus" ]; then
        cp -r "$temp_install_path/include/opus" "$OUTPUT_INCLUDE_DIR/"
    fi
    
    # Go back to base directory
    cd "$BASE_DIR"
}

# Build both libraries
echo "=== Building libraries ==="
build_lib "ogg"
build_lib "opus"

echo
echo "Libraries created in $OUTPUT_DIR:"
ls -l $OUTPUT_DIR
echo
echo "Include files copied to $OUTPUT_INCLUDE_DIR:"
ls -l $OUTPUT_INCLUDE_DIR
