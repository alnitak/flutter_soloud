#!/bin/bash
# The in-process Linux VST3 editor window (src/vst3/vst3_editor_linux.cpp) on
# a real X server: open/close/reopen, the IRunLoop timer and fd handler run
# from the GLib main loop, plug-in initiated resize, the window manager's
# close button, teardown while open, and clean failures (no X11 support in
# the view, no display). Builds test/linux/vst3_editor_linux_test.cpp, whose
# fake IPlugView behaves like a VSTGUI view.
#
#   test/linux/vst3_editor_linux_test.sh [--asan] [--strict]
#
# Platform: Linux x86_64/aarch64. Needs: a C++17 toolchain, libX11 and GLib
# development files (pkg-config x11 glib-2.0), the prebuilt VST3 hosting SDK
# (.dart_tool/flutter_soloud/vst3/prebuild/{include,linux/<x64|arm64>}, or
# VST3_PREBUILD_DIR; see vst3_host_test.sh) and an X display: $DISPLAY, or
# Xvfb, which the script starts itself when DISPLAY is unset. Prints SKIPPED
# when one of them is missing. No audio device, no assets.
#   --asan    AddressSanitizer + UBSan.
#   --strict  known src/ bugs (KNOWN BUG checks) fail the run.
# Verification: Linux arm64, Ubuntu 24.04 in test/docker with Xvfb, also with
# --asan.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

if [ "$(uname -s)" != "Linux" ]; then
    echo "SKIPPED: Linux only"
    exit 0
fi
case "$(uname -m)" in
    x86_64) PREBUILD_ARCH=x64 ;;
    aarch64|arm64) PREBUILD_ARCH=arm64 ;;
    *) echo "SKIPPED: no prebuilt VST3 SDK for $(uname -m)"; exit 0 ;;
esac

SANITIZER=(-O1 -g)
OUT="${TMPDIR:-/tmp}/vst3_editor_linux_test"
ARGS=()
for arg in "$@"; do
    case "$arg" in
        --asan) SANITIZER=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer); OUT="${OUT}_asan" ;;
        --tsan) echo "--tsan: not supported by this test (single-threaded GUI code)" ;;
        --strict) ARGS+=("$arg") ;;
        *) echo "unknown argument: $arg"; exit 2 ;;
    esac
done

PREBUILD="${VST3_PREBUILD_DIR:-$REPO_ROOT/.dart_tool/flutter_soloud/vst3/prebuild}"
SDK_LIB="$PREBUILD/linux/$PREBUILD_ARCH/libvst3_hosting.a"
if [ ! -d "$PREBUILD/include/pluginterfaces" ] || [ ! -f "$SDK_LIB" ]; then
    echo "SKIPPED: VST3 hosting SDK not found ($SDK_LIB). Build the example" \
        "app once with enable_vst3, or set VST3_PREBUILD_DIR."
    exit 0
fi
if ! pkg-config --exists x11 glib-2.0 2>/dev/null; then
    echo "SKIPPED: libX11 / GLib development files not found (pkg-config x11 glib-2.0)"
    exit 0
fi

WORK_DIR="$(mktemp -d)"
XVFB_PID=""
cleanup() {
    [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

FS_LIB=()
if echo 'int main(){}' | c++ -x c++ - -lstdc++fs -o "$WORK_DIR/fs_probe" 2>/dev/null; then
    FS_LIB=(-lstdc++fs)
fi

c++ -std=c++17 -Wall -pthread "${SANITIZER[@]}" \
    -DSOLOUD_ENABLE_VST3 \
    -I src \
    -isystem "$PREBUILD/include" \
    $(pkg-config --cflags x11 glib-2.0) \
    -o "$OUT" \
    test/linux/vst3_editor_linux_test.cpp \
    src/vst3/vst3_editor_linux.cpp \
    "$SDK_LIB" \
    "${FS_LIB[@]}" \
    $(pkg-config --libs x11 glib-2.0) \
    -ldl

if [ "${SANITIZER[*]}" != "-O1 -g" ]; then
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/test/support/lsan.supp${LSAN_OPTIONS:+:$LSAN_OPTIONS}"
fi

echo "=== no display ==="
env -u DISPLAY -u WAYLAND_DISPLAY "$OUT" --no-display

if [ -z "${DISPLAY:-}" ]; then
    if ! command -v Xvfb >/dev/null 2>&1; then
        echo "SKIPPED: no DISPLAY and no Xvfb"
        exit 0
    fi
    for n in $(seq 90 120); do
        [ -e "/tmp/.X11-unix/X$n" ] || [ -e "/tmp/.X$n-lock" ] && continue
        Xvfb ":$n" -screen 0 1280x1024x24 -nolisten tcp > "$WORK_DIR/xvfb.log" 2>&1 &
        XVFB_PID=$!
        export DISPLAY=":$n"
        break
    done
    for _ in $(seq 1 50); do
        [ -e "/tmp/.X11-unix/X${DISPLAY#:}" ] && break
        sleep 0.1
    done
    echo "started Xvfb on $DISPLAY"
fi
echo "=== X display $DISPLAY ==="
"$OUT" ${ARGS[@]+"${ARGS[@]}"}
