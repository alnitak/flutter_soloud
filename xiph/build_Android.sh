#!/bin/bash


# This script builds the Ogg and Opus libraries for Android.
# NOTE: tested only running on Linux.
# Required: Android NDK, CMake, Git

# Exit on any error
set -e

BOLD_WHITE_ON_GREEN="\e[1;37;42m"
RESET="\e[0m"

# Check for Android NDK
if [ -z "$ANDROID_NDK_HOME" ]; then
    echo "Please set ANDROID_NDK_HOME environment variable"
    exit 1
fi

# Directories setup
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/android/build"
OUTPUT_DIR="$BASE_DIR/../android/libs"
OUTPUT_INCLUDE_DIR="$BASE_DIR/../android/include"
ARCHS=("arm64-v8a" "armeabi-v7a" "x86" "x86_64")

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

if [ ! -d "vorbis" ]; then
    git clone https://github.com/xiph/vorbis
    cd vorbis
    git reset --hard 84c0236
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

# Function to build library for specific architecture
build_lib() {
    local lib=$1
    local arch=$2
    local build_path="$BUILD_DIR/${lib}_${arch}"
    local install_path="$OUTPUT_DIR/$arch"
    local temp_install_path="$build_path/install"
    
    echo -e "${BOLD_WHITE_ON_GREEN}Building $lib for $arch...${RESET}"
    echo
    echo -e "${BOLD_WHITE_ON_GREEN}build path: $build_path${RESET}"
    echo -e "${BOLD_WHITE_ON_GREEN}install path: $install_path${RESET}"
    echo -e "${BOLD_WHITE_ON_GREEN}temp install path: $temp_install_path${RESET}"
    
    # Create build directory
    mkdir -p "$build_path"
    mkdir -p "$install_path"
    mkdir -p "$temp_install_path"
    
    # Navigate to build directory
    cd "$build_path"
    
    # Configure and build
    if [ "$lib" = "vorbis" ]; then
        cmake "$BASE_DIR/$lib" \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$arch" \
            -DANDROID_PLATFORM=android-21 \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$temp_install_path" \
            -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_C_FLAGS="-Os -flto -ffunction-sections -fdata-sections" \
            -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384,--gc-sections -flto" \
            -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384,--gc-sections -flto" \
            -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
            -DOGG_LIBRARY="$install_path/libogg.so" \
            -DOGG_INCLUDE_DIR="$OUTPUT_DIR/../include" \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    elif [ "$lib" = "flac" ]; then
        cmake "$BASE_DIR/$lib" \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$arch" \
            -DANDROID_PLATFORM=android-21 \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$temp_install_path" \
            -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_C_FLAGS="-Os -flto -ffunction-sections -fdata-sections" \
            -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384,--gc-sections -flto" \
            -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384,--gc-sections -flto" \
            -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
            -DOGG_LIBRARY="$install_path/libogg.so" \
            -DOGG_INCLUDE_DIR="$OUTPUT_DIR/../include" \
            -DBUILD_PROGRAMS=OFF \
            -DBUILD_EXAMPLES=OFF \
            -DBUILD_TESTING=OFF \
            -DBUILD_DOCS=OFF \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    else
        cmake "$BASE_DIR/$lib" \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI="$arch" \
            -DANDROID_PLATFORM=android-21 \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$temp_install_path" \
            -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_C_FLAGS="-Os -flto -ffunction-sections -fdata-sections" \
            -DCMAKE_EXE_LINKER_FLAGS="-Wl,-z,max-page-size=16384,--gc-sections -flto" \
            -DCMAKE_SHARED_LINKER_FLAGS="-Wl,-z,max-page-size=16384,--gc-sections -flto" \
            -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    fi

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
        elif [ "$lib" = "vorbis" ]; then
            cp -r "$temp_install_path/include/vorbis" "$OUTPUT_INCLUDE_DIR/"
        elif [ "$lib" = "flac" ]; then
            cp -r "$temp_install_path/include/FLAC" "$OUTPUT_INCLUDE_DIR/"
            cp -r "$BASE_DIR/flac/include/share" "$OUTPUT_INCLUDE_DIR/"
        fi
    fi
    
    # Go back to base directory
    cd "$BASE_DIR"
}

# Build both libraries for all architectures
for arch in "${ARCHS[@]}"; do
    echo -e "${BOLD_WHITE_ON_GREEN}=== Building for $arch ===${RESET}"
    build_lib "ogg" "$arch"
    build_lib "opus" "$arch"
    build_lib "vorbis" "$arch"
    build_lib "flac" "$arch"
done

for arch in "${ARCHS[@]}"; do
    echo -e "${BOLD_WHITE_ON_GREEN}=== Removing not used libvorbisenc.so* for $arch ===${RESET}"
    rm "$OUTPUT_DIR/$arch/libvorbisenc.so"*
    echo -e "${BOLD_WHITE_ON_GREEN}=== Removing not used libFLAC++.so* ===${RESET}"
    rm -f "$OUTPUT_DIR/$arch/libFLAC++.so"*
done
rm "$OUTPUT_INCLUDE_DIR/vorbis/vorbisenc.h"

echo
echo -e "${BOLD_WHITE_ON_GREEN}Libraries created in $OUTPUT_DIR:${RESET}"
ls -l $OUTPUT_DIR
echo
echo -e "${BOLD_WHITE_ON_GREEN}Include files copied to $OUTPUT_INCLUDE_DIR:${RESET}"
ls -l $OUTPUT_INCLUDE_DIR