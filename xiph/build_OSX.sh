#!/bin/bash

# This script builds the Ogg, Opus, Vorbis and FLAC libraries for macOS.
# Make sure to install the needed tools: `brew install cmake wget`
#
# NOTE: We use CMake instead of autotools (./configure) because on macOS
# Sequoia+, files may be tagged with the "com.apple.provenance" extended
# attribute. When a script is executed directly (./script), macOS Gatekeeper
# performs a security assessment that can hang indefinitely. Autotools-generated
# configure scripts are shell scripts and are affected by this issue, while
# CMake invokes the compiler directly so it is not affected.

# Exit on any error
set -e

BOLD_WHITE_ON_GREEN=$'\e[1;37;42m'
RESET=$'\e[0m'

# Clone repositories if they don't exist
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

# Directories
LIBS=("ogg" "opus" "vorbis" "flac")
BASE_DIR="$PWD"
BUILD_DIR="$BASE_DIR/macos/build"
OUTPUT_DIR="$BASE_DIR/../macos/flutter_soloud/libs"
INCLUDE_DIR="$BASE_DIR/../macos/flutter_soloud/include"
ARCHS=("arm64" "x86_64")

MACOS_SDK="$(xcrun --sdk macosx --show-sdk-path)"

mkdir -p "$BUILD_DIR"
mkdir -p "$OUTPUT_DIR"
mkdir -p "$INCLUDE_DIR"

# Function to build a library for a specific architecture using CMake
build_lib() {
    local lib_name=$1
    local arch=$2
    local cmake_build_dir="$BUILD_DIR/$lib_name/$arch"
    local install_dir="$cmake_build_dir/install"
    local src_dir="$BASE_DIR/$lib_name"

    echo "${BOLD_WHITE_ON_GREEN}Building $lib_name for macOS ($arch)...${RESET}"

    mkdir -p "$cmake_build_dir"

    local cmake_args=(
        -S "$src_dir"
        -B "$cmake_build_dir"
        -DCMAKE_INSTALL_PREFIX="$install_dir"
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_OSX_ARCHITECTURES="$arch"
        -DCMAKE_OSX_SYSROOT="$MACOS_SDK"
        -DCMAKE_OSX_DEPLOYMENT_TARGET="10.13"
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    )

    if [ "$lib_name" == "ogg" ]; then
        cmake_args+=(
            -DINSTALL_DOCS=OFF
        )
    fi

    if [ "$lib_name" == "opus" ]; then
        cmake_args+=(
            -DOPUS_BUILD_SHARED_LIBRARY=OFF
            -DOPUS_BUILD_TESTING=OFF
            -DOPUS_BUILD_PROGRAMS=OFF
            -DCMAKE_C_FLAGS="-Os -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables"
        )
    fi

    if [ "$lib_name" == "vorbis" ]; then
        local ogg_install="$BUILD_DIR/ogg/$arch/install"
        cmake_args+=(
            -DCMAKE_PREFIX_PATH="$ogg_install"
            -DOGG_INCLUDE_DIR="$ogg_install/include"
            -DOGG_LIBRARY="$ogg_install/lib/libogg.a"
        )
    fi

    if [ "$lib_name" == "flac" ]; then
        local ogg_install="$BUILD_DIR/ogg/$arch/install"
        cmake_args+=(
            -DCMAKE_PREFIX_PATH="$ogg_install"
            -DWITH_OGG=ON
            -DOGG_INCLUDE_DIR="$ogg_install/include"
            -DOGG_LIBRARY="$ogg_install/lib/libogg.a"
            -DBUILD_CXXLIBS=OFF
            -DBUILD_PROGRAMS=OFF
            -DBUILD_EXAMPLES=OFF
            -DBUILD_DOCS=OFF
            -DINSTALL_MANPAGES=OFF
        )
    fi

    cmake "${cmake_args[@]}"
    cmake --build "$cmake_build_dir" -j$(sysctl -n hw.ncpu)
    cmake --install "$cmake_build_dir"
}

# Build libraries for each architecture
for lib in "${LIBS[@]}"; do
    for arch in "${ARCHS[@]}"; do
        build_lib "$lib" "$arch"
    done
done

# Create universal binaries and copy include files
for lib in "${LIBS[@]}"; do
    echo "${BOLD_WHITE_ON_GREEN}Creating universal binary for $lib...${RESET}"

    if [ "$lib" == "flac" ]; then
        lipo -create \
            "$BUILD_DIR/$lib/arm64/install/lib/libFLAC.a" \
            "$BUILD_DIR/$lib/x86_64/install/lib/libFLAC.a" \
            -output "$OUTPUT_DIR/libFLAC.a"
    elif [ "$lib" == "vorbis" ]; then
        lipo -create \
            "$BUILD_DIR/$lib/arm64/install/lib/libvorbis.a" \
            "$BUILD_DIR/$lib/x86_64/install/lib/libvorbis.a" \
            -output "$OUTPUT_DIR/libvorbis.a"

        echo "${BOLD_WHITE_ON_GREEN}Creating universal binary for vorbisfile...${RESET}"
        lipo -create \
            "$BUILD_DIR/$lib/arm64/install/lib/libvorbisfile.a" \
            "$BUILD_DIR/$lib/x86_64/install/lib/libvorbisfile.a" \
            -output "$OUTPUT_DIR/libvorbisfile.a"
    else
        lipo -create \
            "$BUILD_DIR/$lib/arm64/install/lib/lib${lib}.a" \
            "$BUILD_DIR/$lib/x86_64/install/lib/lib${lib}.a" \
            -output "$OUTPUT_DIR/lib${lib}.a"
    fi

    # Copy include files (from either arch, they're the same)
    cp -R "$BUILD_DIR/$lib/arm64/install/include/"* "$INCLUDE_DIR/"
done

# Copy FLAC share headers
if [ -d "$BASE_DIR/flac/include/share" ]; then
    cp -R "$BASE_DIR/flac/include/share" "$INCLUDE_DIR/"
fi

# Strip symbols from libraries
for lib in "${LIBS[@]}"; do
    echo "${BOLD_WHITE_ON_GREEN}Stripping symbols from $lib...${RESET}"
    if [ "$lib" == "flac" ]; then
        strip -x "$OUTPUT_DIR/libFLAC.a"
    else
        strip -x "$OUTPUT_DIR/lib${lib}.a"
    fi
    if [ "$lib" == "vorbis" ]; then
        echo "${BOLD_WHITE_ON_GREEN}Stripping symbols from vorbisfile...${RESET}"
        strip -x "$OUTPUT_DIR/libvorbisfile.a"
    fi
done

echo
echo "${BOLD_WHITE_ON_GREEN}Libraries created in $OUTPUT_DIR:${RESET}"
ls -l "$OUTPUT_DIR"
echo
echo "${BOLD_WHITE_ON_GREEN}Include files copied to $INCLUDE_DIR:${RESET}"
ls -l "$INCLUDE_DIR"

echo
echo "${BOLD_WHITE_ON_GREEN}Creating XCFrameworks for macOS...${RESET}"
FRAMEWORKS_DIR="$BASE_DIR/../macos/flutter_soloud/Frameworks"
rm -rf "$FRAMEWORKS_DIR"
mkdir -p "$FRAMEWORKS_DIR"

for lib in "${LIBS[@]}"; do
    local_lib_name="$lib"
    if [ "$lib" == "flac" ]; then
        local_lib_name="FLAC"
    fi

    echo "${BOLD_WHITE_ON_GREEN}Creating ${lib}.xcframework...${RESET}"
    xcodebuild -create-xcframework \
        -library "$OUTPUT_DIR/lib${local_lib_name}.a" \
        -output "$FRAMEWORKS_DIR/${lib}.xcframework" > /dev/null

    if [ "$lib" == "vorbis" ]; then
        echo "${BOLD_WHITE_ON_GREEN}Creating vorbisfile.xcframework...${RESET}"
        xcodebuild -create-xcframework \
            -library "$OUTPUT_DIR/libvorbisfile.a" \
            -output "$FRAMEWORKS_DIR/vorbisfile.xcframework" > /dev/null
    fi
done

echo
echo "${BOLD_WHITE_ON_GREEN}XCFrameworks created successfully in $FRAMEWORKS_DIR${RESET}"
ls -l "$FRAMEWORKS_DIR"

echo
echo "${BOLD_WHITE_ON_GREEN}Creating SPM symlink to include folder...${RESET}"
mkdir -p "$BASE_DIR/../macos/flutter_soloud/Sources/flutter_soloud"
rm -f "$BASE_DIR/../macos/flutter_soloud/Sources/flutter_soloud/include"
ln -s ../../include "$BASE_DIR/../macos/flutter_soloud/Sources/flutter_soloud/include"
