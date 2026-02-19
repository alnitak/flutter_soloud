#!/bin/bash

# This script is invoked by the CocoaPods script_phase during Xcode builds.
# It uses CMake to build the flutter_soloud plugin as a static library with
# release-mode optimizations, regardless of the app's build configuration.
#
# CMake's internal dependency tracking handles incremental builds — if no
# source files changed, this is a fast no-op.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Use Xcode environment variables
# ARCHS: space-separated list of architectures (e.g., "arm64" or "arm64 x86_64")
# SDKROOT: path to the SDK

if [ -z "$ARCHS" ]; then
    # Not running from Xcode — default to native architecture
    ARCHS=$(uname -m)
    echo "ARCHS not set, defaulting to: ${ARCHS}"
fi

if [ -z "$SDKROOT" ]; then
    SDKROOT=$(xcrun --sdk macosx --show-sdk-path)
    echo "SDKROOT not set, defaulting to: ${SDKROOT}"
fi

# Convert space-separated ARCHS to semicolons for CMake
CMAKE_ARCHS=$(echo "$ARCHS" | tr ' ' ';')

BUILD_DIR="${SCRIPT_DIR}/cmake_build/macosx"

echo "=== flutter_soloud: CMake build for macOS ==="
echo "  ARCHS: ${ARCHS}"
echo "  CMAKE_ARCHS: ${CMAKE_ARCHS}"
echo "  SDKROOT: ${SDKROOT}"
echo "  BUILD_DIR: ${BUILD_DIR}"

# Pass NO_OPUS_OGG_LIBS to CMake if set
CMAKE_EXTRA_ARGS=""
if [ -n "$NO_OPUS_OGG_LIBS" ]; then
    CMAKE_EXTRA_ARGS="-DNO_OPUS_OGG_LIBS=ON"
fi

cmake -S "${SCRIPT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="${CMAKE_ARCHS}" \
    -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="10.15" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    ${CMAKE_EXTRA_ARGS}

cmake --build "${BUILD_DIR}" -j$(sysctl -n hw.ncpu)

echo "=== flutter_soloud: CMake build complete ==="
echo "  Library: ${BUILD_DIR}/libflutter_soloud_plugin.a"
