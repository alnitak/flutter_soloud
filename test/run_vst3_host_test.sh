#!/bin/bash
# Build and run the standalone native VST3 host tests (macOS only):
#
#   ./test/run_vst3_host_test.sh [--asan|--tsan] [test name]
#
# They need the prebuilt VST3 hosting SDK that the build hook downloads when
# VST3 is enabled (the `enable_vst3` user define, or SOLOUD_ENABLE_VST3=1),
# from .dart_tool/flutter_soloud/vst3/prebuild, or from VST3_PREBUILD_DIR.
#
# --asan builds under AddressSanitizer and UndefinedBehaviorSanitizer,
# --tsan under ThreadSanitizer.
#
# Elsewhere, or without the SDK, it prints a SKIP line and exits 0.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname)" != "Darwin" ]; then
    echo "SKIP: The VST3 host tests run on macOS only."
    exit 0
fi

PREBUILD="${VST3_PREBUILD_DIR:-$REPO_ROOT/.dart_tool/flutter_soloud/vst3/prebuild}"
SDK_LIB="$PREBUILD/macos/libvst3_hosting.a"
if [ ! -d "$PREBUILD/include/pluginterfaces" ] || [ ! -f "$SDK_LIB" ]; then
    echo "SKIP: VST3 hosting SDK not found in $PREBUILD. Build the example" \
        "app once (it enables VST3), or set VST3_PREBUILD_DIR."
    exit 0
fi

SANITIZER=(-O1 -g)
WORK="${TMPDIR:-/tmp}/vst3_host_test"
case "$1" in
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
mkdir -p "$WORK"

# The mock plug-in, as a .vst3 bundle.
make_bundle() {
    local bundle="$1" name="$2"
    mkdir -p "$bundle/Contents/MacOS"
    cat > "$bundle/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>$name</string>
    <key>CFBundleIdentifier</key>
    <string>dev.flutter-soloud.test.$name</string>
    <key>CFBundlePackageType</key>
    <string>BNDL</string>
</dict>
</plist>
EOF
}

make_bundle "$WORK/MockVst3.vst3" MockVst3
c++ -std=c++17 -Wall -bundle "${SANITIZER[@]}" \
    -I test \
    -isystem "$PREBUILD/include" \
    -o "$WORK/MockVst3.vst3/Contents/MacOS/MockVst3" \
    test/vst3_mock_plugin.cpp \
    "$SDK_LIB" \
    -framework CoreFoundation

c++ -std=c++17 -Wall -pthread "${SANITIZER[@]}" \
    -DSOLOUD_ENABLE_VST3 \
    -I src \
    -I test \
    -isystem "$PREBUILD/include" \
    -o "$WORK/vst3_host_test" \
    test/vst3_host_test.mm \
    src/vst3/vst3_host.cpp \
    src/vst3/vst3_editor_macos.mm \
    "$SDK_LIB" \
    -framework Cocoa \
    -framework QuartzCore \
    -framework CoreFoundation

"$WORK/vst3_host_test" "$WORK" "$@"
