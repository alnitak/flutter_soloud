#!/bin/bash
# The macOS in-process VST3 editor window (src/vst3/vst3_editor_macos.mm) with
# a mock IPlugView that attaches a real NSView: open/close from the host,
# close from the window, two editors at once, resizeView from the plug-in,
# open from a background thread, refused views, reopening. Builds and runs
# test/macos/vst3_editor_macos_test.mm.
#
#   test/macos/vst3_editor_macos_test.sh [--asan]
#
# Platform: macOS with a logged-in GUI session (windows appear briefly).
# Needs the prebuilt VST3 hosting SDK the build hook downloads when VST3 is
# enabled (.dart_tool/flutter_soloud/vst3/prebuild, or VST3_PREBUILD_DIR).
# Prints SKIPPED elsewhere, without the SDK, or without a window server
# (SSH session). No audio device, no assets.
#   --asan  build with AddressSanitizer and UBSan.
# Verification: verified on macOS arm64.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname)" != "Darwin" ]; then
    echo "SKIPPED: macOS only"
    exit 0
fi
PREBUILD="${VST3_PREBUILD_DIR:-$REPO_ROOT/.dart_tool/flutter_soloud/vst3/prebuild}"
SDK_LIB="$PREBUILD/macos/libvst3_hosting.a"
if [ ! -d "$PREBUILD/include/pluginterfaces" ] || [ ! -f "$SDK_LIB" ]; then
    echo "SKIPPED: VST3 hosting SDK not found in $PREBUILD (build the example app once, or set VST3_PREBUILD_DIR)."
    exit 0
fi
if ! launchctl managername 2>/dev/null | grep -q Aqua; then
    echo "SKIPPED: no GUI session (windows cannot be opened)"
    exit 0
fi

OUT="${TMPDIR:-/tmp}/vst3_editor_macos_test"
SANITIZER=(-O1 -g)
if [ "${1:-}" == "--asan" ]; then
    SANITIZER=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    OUT="${OUT}_asan"
fi

clang++ -std=c++17 -Wall -fobjc-arc "${SANITIZER[@]}" \
    -DSOLOUD_ENABLE_VST3 \
    -I src \
    -I src/vst3 \
    -isystem "$PREBUILD/include" \
    -o "$OUT" \
    test/macos/vst3_editor_macos_test.mm \
    src/vst3/vst3_editor_macos.mm \
    "$SDK_LIB" \
    -framework Cocoa -framework QuartzCore -framework CoreFoundation

"$OUT"
