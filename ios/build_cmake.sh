#!/bin/bash

# This script is invoked by the CocoaPods script_phase during Xcode builds.
# It uses CMake to build the flutter_soloud plugin as a static library with
# release-mode optimizations, regardless of the app's build configuration.
#
# CMake's internal dependency tracking handles incremental builds — if no
# source files changed, this is a fast no-op.
#
# NOTE: SCRIPT_DIR is resolved to its physical path (pwd -P). When invoked
# by CocoaPods, BASH_SOURCE points into the Flutter .symlinks tree, which
# is a different absolute path than the real plugin directory. Using the
# physical path everywhere keeps a single CMake source/build registration,
# so incremental builds work no matter which path this script is invoked
# through.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"

# Xcode's build environment has a restricted PATH that may not include cmake.
# Add common locations where cmake might be installed.
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

# Verify cmake is available
if ! command -v cmake &> /dev/null; then
    echo "ERROR: cmake not found. Please install cmake (e.g., 'brew install cmake')"
    exit 1
fi

echo "  Using cmake: $(which cmake)"

# Use Xcode environment variables
# PLATFORM_NAME: iphoneos, iphonesimulator
# ARCHS: space-separated list of architectures (e.g., "arm64" or "arm64 x86_64")
# SDKROOT: path to the SDK

if [ -z "$PLATFORM_NAME" ]; then
    echo "ERROR: PLATFORM_NAME is not set. This script must be run from Xcode."
    exit 1
fi

if [ -z "$ARCHS" ]; then
    echo "ERROR: ARCHS is not set. This script must be run from Xcode."
    exit 1
fi

if [ -z "$SDKROOT" ]; then
    echo "ERROR: SDKROOT is not set. This script must be run from Xcode."
    exit 1
fi

# Convert space-separated ARCHS to semicolons for CMake
CMAKE_ARCHS=$(echo "$ARCHS" | tr ' ' ';')

BUILD_DIR="${SCRIPT_DIR}/cmake_build/${PLATFORM_NAME}"

# If this platform's build directory was configured with a different source
# path (e.g. via the CocoaPods symlink before SCRIPT_DIR was normalized to
# its physical path), CMake would refuse to reconfigure or silently reuse
# stale objects. Wipe only this platform's build dir in that case, leaving
# other platforms' build dirs (and their incremental caches) untouched.
if [ -f "${BUILD_DIR}/CMakeCache.txt" ] &&
    ! grep -q "^CMAKE_HOME_DIRECTORY[A-Z:]*=${SCRIPT_DIR}\$" "${BUILD_DIR}/CMakeCache.txt"; then
    echo "  Build dir was configured with a different source path, wiping it."
    rm -rf "${BUILD_DIR}"
fi

echo "=== flutter_soloud: CMake build for iOS ==="
echo "  PLATFORM_NAME: ${PLATFORM_NAME}"
echo "  ARCHS: ${ARCHS}"
echo "  CMAKE_ARCHS: ${CMAKE_ARCHS}"
echo "  SDKROOT: ${SDKROOT}"
echo "  BUILD_DIR: ${BUILD_DIR}"

# Clear cached NO_XIPH_LIBS from CMake cache to ensure environment variable is respected
# This allows switching between configurations without manual cache deletion
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    # Remove any cached NO_XIPH_LIBS value so we always use the current environment
    sed -i '' '/NO_XIPH_LIBS/d' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null || true
fi

cmake -S "${SCRIPT_DIR}" \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES="${CMAKE_ARCHS}" \
    -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="13.0" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5

cmake --build "${BUILD_DIR}" -j$(sysctl -n hw.ncpu)

echo "=== flutter_soloud: CMake build complete ==="
echo "  Library: ${BUILD_DIR}/libflutter_soloud_plugin.a"
