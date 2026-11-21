#!/bin/bash
# Build and run the standalone native VST3 host tests on Linux:
# test/common/vst3_host_test.cpp (the tests are in
# test/support/vst3_host_checks.h) with src/vst3/vst3_host.cpp and the Linux
# editor (src/vst3/vst3_editor_linux.cpp), against the mock plug-in and editor
# helper in test/support/, laid out as Linux .vst3 bundles
# (Contents/<arch>-linux/<name>.so).
#
#   test/linux/vst3_host_test.sh [--asan|--tsan] [test name]
#   tests: params instrument editor remote-editor exit
#
# Platform: Linux x86_64/aarch64. Needs: a C++17 toolchain and the prebuilt
# VST3 hosting SDK the build hook downloads when VST3 is enabled (the
# `enable_vst3` user define): .dart_tool/flutter_soloud/vst3/prebuild/
# {include,linux/<x64|arm64>/libvst3_hosting.a}, or VST3_PREBUILD_DIR. Prints
# SKIPPED without it or off Linux. No audio device, no assets, no display:
# the remote-editor test runs the helper's poll timer through GLib
# (libglib-2.0.so.0, loaded at run time) and is not run without it.
#   --asan  AddressSanitizer + UBSan;  --tsan  ThreadSanitizer.
# Verification: NOT YET VERIFIED ON LINUX (written on macOS, where the same
# test source passes against the macOS host).

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Linux" ]; then
    echo "SKIPPED: Linux only (macOS: test/macos/vst3_host_test.sh)"
    exit 0
fi

case "$(uname -m)" in
    x86_64) ARCH_DIR=x86_64-linux; PREBUILD_ARCH=x64 ;;
    aarch64|arm64) ARCH_DIR=aarch64-linux; PREBUILD_ARCH=arm64 ;;
    *) echo "SKIPPED: no prebuilt VST3 SDK for $(uname -m)"; exit 0 ;;
esac

PREBUILD="${VST3_PREBUILD_DIR:-$REPO_ROOT/.dart_tool/flutter_soloud/vst3/prebuild}"
SDK_LIB="$PREBUILD/linux/$PREBUILD_ARCH/libvst3_hosting.a"
if [ ! -d "$PREBUILD/include/pluginterfaces" ] || [ ! -f "$SDK_LIB" ]; then
    echo "SKIPPED: VST3 hosting SDK not found ($SDK_LIB). Build the example" \
        "app once with enable_vst3, or set VST3_PREBUILD_DIR."
    exit 0
fi

SANITIZER=(-O1 -g)
WORK="${TMPDIR:-/tmp}/vst3_host_test_linux"
case "${1:-}" in
    --asan)
        SANITIZER=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        WORK="${WORK}_asan"
        shift
        ;;
    --tsan)
        SANITIZER=(-O1 -g -fsanitize=thread)
        WORK="${WORK}_tsan"
        shift
        ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

# Older libstdc++ keeps std::filesystem (used by the SDK's module loader) in
# a separate library.
FS_LIB=()
if echo 'int main(){}' | c++ -x c++ - -lstdc++fs -o "$WORK/fs_probe" 2>/dev/null; then
    FS_LIB=(-lstdc++fs)
fi

# The mock plug-in, as a Linux .vst3 bundle.
mkdir -p "$WORK/MockVst3.vst3/Contents/$ARCH_DIR"
c++ -std=c++17 -Wall -shared -fPIC "${SANITIZER[@]}" \
    -I test/support \
    -isystem "$PREBUILD/include" \
    -o "$WORK/MockVst3.vst3/Contents/$ARCH_DIR/MockVst3.so" \
    test/support/vst3_mock_plugin.cpp \
    "$SDK_LIB" \
    -pthread

# The same plug-in in a bundle that embeds a Flutter app (the marker the Linux
# editor looks for), whose editor the host opens in the helper stand-in.
mkdir -p "$WORK/MockVst3Remote.vst3/Contents/$ARCH_DIR/data/flutter_assets"
cp "$WORK/MockVst3.vst3/Contents/$ARCH_DIR/MockVst3.so" \
    "$WORK/MockVst3Remote.vst3/Contents/$ARCH_DIR/MockVst3Remote.so"
c++ -std=c++17 -Wall -O1 \
    -DSOLOUD_ENABLE_VST3 \
    -I src \
    -o "$WORK/vst3_mock_editor_helper" \
    test/support/vst3_mock_editor_helper.cpp

c++ -std=c++17 -Wall -pthread "${SANITIZER[@]}" \
    -DSOLOUD_ENABLE_VST3 \
    -DSOLOUD_VST3_HELPER_PATH="\"$WORK/vst3_mock_editor_helper\"" \
    -I src \
    -I test/support \
    -isystem "$PREBUILD/include" \
    -o "$WORK/vst3_host_test" \
    test/common/vst3_host_test.cpp \
    src/vst3/vst3_host.cpp \
    src/vst3/vst3_editor_linux.cpp \
    "$SDK_LIB" \
    "${FS_LIB[@]}" \
    -ldl

"$WORK/vst3_host_test" "$WORK" "$@"
